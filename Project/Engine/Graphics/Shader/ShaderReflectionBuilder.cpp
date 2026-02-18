#include "ShaderReflectionBuilder.h"
#include "Engine/Utility/Logger/Logger.h"
#include <cassert>

namespace CoreEngine
{
    void ShaderReflectionBuilder::Initialize(IDxcUtils* dxcUtils) {
        assert(dxcUtils != nullptr);
        dxcUtils_ = dxcUtils;
    }

    std::unique_ptr<ShaderReflectionData> ShaderReflectionBuilder::BuildFromShaders(
        IDxcBlob* vertexShaderBlob,
        IDxcBlob* pixelShaderBlob) {

        auto reflectionData = std::make_unique<ShaderReflectionData>();

        // 頂点シェーダーをリフレクション
        if (vertexShaderBlob) {
            ReflectShader(vertexShaderBlob, D3D12_SHADER_VISIBILITY_VERTEX, *reflectionData);
        }

        // ピクセルシェーダーをリフレクション
        if (pixelShaderBlob) {
            ShaderReflectionData psData;
            ReflectShader(pixelShaderBlob, D3D12_SHADER_VISIBILITY_PIXEL, psData);
            reflectionData->Merge(psData);
        }

        // デバッグ出力
#ifdef _DEBUG
        Logger::GetInstance().Log(reflectionData->ToString(), LogLevel::INFO, LogCategory::Shader);
#endif

        return reflectionData;
    }

    std::unique_ptr<ShaderReflectionData> ShaderReflectionBuilder::BuildFromShader(
        IDxcBlob* shaderBlob,
        D3D12_SHADER_VISIBILITY visibility) {

        auto reflectionData = std::make_unique<ShaderReflectionData>();

        if (shaderBlob) {
            ReflectShader(shaderBlob, visibility, *reflectionData);
        }

#ifdef _DEBUG
        Logger::GetInstance().Log(reflectionData->ToString(), LogLevel::INFO, LogCategory::Shader);
#endif

        return reflectionData;
    }

    std::unique_ptr<ShaderReflectionData> ShaderReflectionBuilder::BuildFromComputeShader(
        IDxcBlob* computeShaderBlob) {

        return BuildFromShader(computeShaderBlob, D3D12_SHADER_VISIBILITY_ALL);
    }

    void ShaderReflectionBuilder::ReflectShader(
        IDxcBlob* shaderBlob,
        D3D12_SHADER_VISIBILITY visibility,
        ShaderReflectionData& outData) {

        assert(dxcUtils_ != nullptr);
        assert(shaderBlob != nullptr);

        // DXCからリフレクションインターフェースを作成
        DxcBuffer reflectionBuffer;
        reflectionBuffer.Ptr = shaderBlob->GetBufferPointer();
        reflectionBuffer.Size = shaderBlob->GetBufferSize();
        reflectionBuffer.Encoding = 0;

        Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
        HRESULT hr = dxcUtils_->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(reflection.GetAddressOf()));

        if (FAILED(hr)) {
            Logger::GetInstance().Log("Failed to create shader reflection", LogLevel::Error, LogCategory::Shader);
            return;
        }

        // シェーダー情報を取得
        D3D12_SHADER_DESC shaderDesc;
        reflection->GetDesc(&shaderDesc);

        // 定数バッファをリフレクション
        ReflectConstantBuffers(reflection.Get(), visibility, outData);

        // バウンドリソースをリフレクション
        ReflectBoundResources(reflection.Get(), visibility, outData);

        // 入力レイアウトをリフレクション（頂点シェーダーのみ）
        if (visibility == D3D12_SHADER_VISIBILITY_VERTEX) {
            ReflectInputLayout(reflection.Get(), outData);
        }
    }

    void ShaderReflectionBuilder::ReflectConstantBuffers(
        ID3D12ShaderReflection* reflection,
        D3D12_SHADER_VISIBILITY visibility,
        ShaderReflectionData& outData) {

        D3D12_SHADER_DESC shaderDesc;
        reflection->GetDesc(&shaderDesc);

#ifdef _DEBUG
        Logger::GetInstance().Log("=== ReflectConstantBuffers Debug ===", LogLevel::INFO, LogCategory::Shader);
        Logger::GetInstance().Log("Total ConstantBuffers: " + std::to_string(shaderDesc.ConstantBuffers), LogLevel::INFO, LogCategory::Shader);
#endif

        for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i) {
            ID3D12ShaderReflectionConstantBuffer* cbuffer = reflection->GetConstantBufferByIndex(i);
            D3D12_SHADER_BUFFER_DESC bufferDesc;
            cbuffer->GetDesc(&bufferDesc);

#ifdef _DEBUG
            Logger::GetInstance().Log(
                "  [" + std::to_string(i) + "] " + std::string(bufferDesc.Name) + 
                " : Size=" + std::to_string(bufferDesc.Size) + " bytes",
                LogLevel::INFO, LogCategory::Shader);
#endif

            // $Globalsは除外（グローバル変数用の内部バッファ）
            if (std::string(bufferDesc.Name).find("$Globals") != std::string::npos) {
#ifdef _DEBUG
                Logger::GetInstance().Log("    -> Skipped ($Globals)", LogLevel::INFO, LogCategory::Shader);
#endif
                continue;
            }

            ShaderResourceBinding binding;
            binding.name = bufferDesc.Name;
            binding.type = D3D_SIT_CBUFFER;
            binding.size = bufferDesc.Size;
            binding.visibility = visibility;

            // バインドポイントを取得
            D3D12_SHADER_INPUT_BIND_DESC bindDesc;
            bool found = false;
            bool isStructuredBuffer = false;
            for (UINT j = 0; j < shaderDesc.BoundResources; ++j) {
                reflection->GetResourceBindingDesc(j, &bindDesc);
                if (std::string(bindDesc.Name) == bufferDesc.Name) {
                    // StructuredBufferの場合はスキップ（ReflectBoundResourcesで処理される）
                    if (bindDesc.Type == D3D_SIT_STRUCTURED) {
                        isStructuredBuffer = true;
#ifdef _DEBUG
                        Logger::GetInstance().Log("    -> Skipped (StructuredBuffer)", LogLevel::INFO, LogCategory::Shader);
#endif
                        break;
                    }
                    
                    binding.bindPoint = bindDesc.BindPoint;
                    binding.bindCount = bindDesc.BindCount;
                    binding.space = bindDesc.Space;
                    found = true;
#ifdef _DEBUG
                    Logger::GetInstance().Log(
                        "    -> Found in BoundResources: b" + std::to_string(bindDesc.BindPoint) +
                        ", space=" + std::to_string(bindDesc.Space),
                        LogLevel::INFO, LogCategory::Shader);
#endif
                    break;
                }
            }

            if (isStructuredBuffer) {
                continue;
            }

#ifdef _DEBUG
            if (!found) {
                Logger::GetInstance().Log("    -> NOT found in BoundResources!", LogLevel::WARNING, LogCategory::Shader);
            }
#endif

            outData.AddCBV(binding);
        }
    }

    void ShaderReflectionBuilder::ReflectBoundResources(
        ID3D12ShaderReflection* reflection,
        D3D12_SHADER_VISIBILITY visibility,
        ShaderReflectionData& outData) {

        D3D12_SHADER_DESC shaderDesc;
        reflection->GetDesc(&shaderDesc);

#ifdef _DEBUG
        Logger::GetInstance().Log("=== ReflectBoundResources Debug ===", LogLevel::INFO, LogCategory::Shader);
        Logger::GetInstance().Log("Total BoundResources: " + std::to_string(shaderDesc.BoundResources), LogLevel::INFO, LogCategory::Shader);
#endif

        for (UINT i = 0; i < shaderDesc.BoundResources; ++i) {
            D3D12_SHADER_INPUT_BIND_DESC bindDesc;
            reflection->GetResourceBindingDesc(i, &bindDesc);

#ifdef _DEBUG
            std::string typeStr;
            switch (bindDesc.Type) {
            case D3D_SIT_CBUFFER: typeStr = "CBUFFER"; break;
            case D3D_SIT_TBUFFER: typeStr = "TBUFFER"; break;
            case D3D_SIT_TEXTURE: typeStr = "TEXTURE"; break;
            case D3D_SIT_SAMPLER: typeStr = "SAMPLER"; break;
            case D3D_SIT_UAV_RWTYPED: typeStr = "UAV_RWTYPED"; break;
            case D3D_SIT_STRUCTURED: typeStr = "STRUCTURED"; break;
            case D3D_SIT_UAV_RWSTRUCTURED: typeStr = "UAV_RWSTRUCTURED"; break;
            case D3D_SIT_BYTEADDRESS: typeStr = "BYTEADDRESS"; break;
            case D3D_SIT_UAV_RWBYTEADDRESS: typeStr = "UAV_RWBYTEADDRESS"; break;
            default: typeStr = "UNKNOWN(" + std::to_string(bindDesc.Type) + ")"; break;
            }
            Logger::GetInstance().Log(
                "  [" + std::to_string(i) + "] " + std::string(bindDesc.Name) + 
                " : Type=" + typeStr + 
                ", BindPoint=" + std::to_string(bindDesc.BindPoint) + 
                ", Space=" + std::to_string(bindDesc.Space),
                LogLevel::INFO, LogCategory::Shader);
#endif

            ShaderResourceBinding binding;
            binding.name = bindDesc.Name;
            binding.type = bindDesc.Type;
            binding.bindPoint = bindDesc.BindPoint;
            binding.bindCount = bindDesc.BindCount;
            binding.space = bindDesc.Space;
            binding.visibility = visibility;

            switch (bindDesc.Type) {
            case D3D_SIT_TEXTURE:
            case D3D_SIT_STRUCTURED:
            case D3D_SIT_BYTEADDRESS:
                // SRV (Texture, StructuredBuffer, ByteAddressBuffer)
                outData.AddSRV(binding);
                break;

            case D3D_SIT_UAV_RWTYPED:
            case D3D_SIT_UAV_RWSTRUCTURED:
            case D3D_SIT_UAV_RWBYTEADDRESS:
            case D3D_SIT_UAV_APPEND_STRUCTURED:
            case D3D_SIT_UAV_CONSUME_STRUCTURED:
            case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
                // UAV (RWTexture, RWStructuredBuffer等)
                outData.AddUAV(binding);
                break;

            case D3D_SIT_SAMPLER:
                // Sampler
                outData.AddSampler(binding);
                break;

            case D3D_SIT_CBUFFER:
                // ConstantBuffer<T>構文の場合、ConstantBuffersに含まれない可能性があるため
                // ここで処理する
                outData.AddCBV(binding);
                break;

            default:
                break;
            }
        }
    }

    void ShaderReflectionBuilder::ReflectInputLayout(
        ID3D12ShaderReflection* reflection,
        ShaderReflectionData& outData) {

        D3D12_SHADER_DESC shaderDesc;
        reflection->GetDesc(&shaderDesc);

        for (UINT i = 0; i < shaderDesc.InputParameters; ++i) {
            D3D12_SIGNATURE_PARAMETER_DESC paramDesc;
            reflection->GetInputParameterDesc(i, &paramDesc);

            // システム値は除外（SV_VertexID等）
            if (paramDesc.SystemValueType != D3D_NAME_UNDEFINED) {
                continue;
            }

            InputElementInfo element;
            element.semanticName = paramDesc.SemanticName;
            element.semanticIndex = paramDesc.SemanticIndex;
            element.format = GetDXGIFormat(paramDesc.ComponentType, paramDesc.Mask);
            element.inputSlot = 0;  // 基本的には0
            element.alignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

            outData.AddInputElement(element);
        }
    }

    DXGI_FORMAT ShaderReflectionBuilder::GetDXGIFormat(
        D3D_REGISTER_COMPONENT_TYPE componentType,
        BYTE mask) {

        // マスクからコンポーネント数を計算
        UINT componentCount = 0;
        if (mask & 0x1) componentCount++;
        if (mask & 0x2) componentCount++;
        if (mask & 0x4) componentCount++;
        if (mask & 0x8) componentCount++;

        // 型とコンポーネント数からフォーマットを決定
        switch (componentType) {
        case D3D_REGISTER_COMPONENT_FLOAT32:
            switch (componentCount) {
            case 1: return DXGI_FORMAT_R32_FLOAT;
            case 2: return DXGI_FORMAT_R32G32_FLOAT;
            case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
            case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
            }
            break;

        case D3D_REGISTER_COMPONENT_UINT32:
            switch (componentCount) {
            case 1: return DXGI_FORMAT_R32_UINT;
            case 2: return DXGI_FORMAT_R32G32_UINT;
            case 3: return DXGI_FORMAT_R32G32B32_UINT;
            case 4: return DXGI_FORMAT_R32G32B32A32_UINT;
            }
            break;

        case D3D_REGISTER_COMPONENT_SINT32:
            switch (componentCount) {
            case 1: return DXGI_FORMAT_R32_SINT;
            case 2: return DXGI_FORMAT_R32G32_SINT;
            case 3: return DXGI_FORMAT_R32G32B32_SINT;
            case 4: return DXGI_FORMAT_R32G32B32A32_SINT;
            }
            break;
        }

        return DXGI_FORMAT_UNKNOWN;
    }
}
