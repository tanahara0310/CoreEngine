#include "pch.h"
#include "ShaderMaterialPipeline.h"

#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Material/ShaderMaterialLibrary.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/Logger/Logger.h"

#include <d3d12shader.h>
#include <dxcapi.h>
#include <wrl.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace CoreEngine
{
    namespace
    {
        /// @brief 部品のテクスチャの名前（エンジンが差す）
        constexpr const char* kMainTextureName = "gTexture";

        /// @brief シェーダの型から値の項目の種類を決める
        /// @return 扱える型なら true
        bool ToFieldKind(D3D_SHADER_VARIABLE_TYPE type, MaterialParamField::Kind& kind)
        {
            switch (type) {
            case D3D_SVT_FLOAT: kind = MaterialParamField::Kind::Float; return true;
            case D3D_SVT_INT:   kind = MaterialParamField::Kind::Int;   return true;
            case D3D_SVT_UINT:  kind = MaterialParamField::Kind::Uint;  return true;
            case D3D_SVT_BOOL:  kind = MaterialParamField::Kind::Bool;  return true;
            default:            return false;
            }
        }

        /// @brief 項目 1 つを並びへ足す（数・ベクトルとその配列だけ扱う）
        void AddField(MaterialParamLayout& layout, const char* name, const D3D12_SHADER_TYPE_DESC& type,
            uint32_t offset, const std::string& shaderName)
        {
            MaterialParamField field;
            const bool scalarOrVector = type.Class == D3D_SVC_SCALAR || type.Class == D3D_SVC_VECTOR;
            if (!name || !scalarOrVector || !ToFieldKind(type.Type, field.kind)) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Shader,
                    "マテリアルのシェーダ {} の値 {} は、数とベクトル（とその配列）でないのでマテリアルから決められません",
                    shaderName, name ? name : "?");
                return;
            }
            field.name = name;
            field.offset = offset;
            field.components = (std::clamp)(static_cast<uint32_t>(type.Columns), 1u, 4u);
            field.elements = type.Elements;
            layout.fields.push_back(std::move(field));
        }

        /// @brief 定数バッファの項目を読む（`cbuffer X { … }` と `ConstantBuffer<T>` の両方）
        void ReadConstantBuffer(ID3D12ShaderReflection* reflection, const char* bufferName,
            MaterialParamLayout& layout, const std::string& shaderName)
        {
            ID3D12ShaderReflectionConstantBuffer* const buffer = reflection->GetConstantBufferByName(bufferName);
            D3D12_SHADER_BUFFER_DESC bufferDesc{};
            if (!buffer || FAILED(buffer->GetDesc(&bufferDesc))) {
                return;
            }
            layout.bufferName = bufferName;
            layout.size = bufferDesc.Size;

            for (UINT index = 0; index < bufferDesc.Variables; ++index) {
                ID3D12ShaderReflectionVariable* const variable = buffer->GetVariableByIndex(index);
                D3D12_SHADER_VARIABLE_DESC variableDesc{};
                if (!variable || FAILED(variable->GetDesc(&variableDesc))) {
                    continue;
                }
                ID3D12ShaderReflectionType* const type = variable->GetType();
                D3D12_SHADER_TYPE_DESC typeDesc{};
                if (!type || FAILED(type->GetDesc(&typeDesc))) {
                    continue;
                }

                // ConstantBuffer<T> は構造体の変数 1 つに包まれて見える
                if (typeDesc.Class == D3D_SVC_STRUCT && typeDesc.Elements == 0) {
                    for (UINT member = 0; member < typeDesc.Members; ++member) {
                        ID3D12ShaderReflectionType* const memberType = type->GetMemberTypeByIndex(member);
                        D3D12_SHADER_TYPE_DESC memberDesc{};
                        if (!memberType || FAILED(memberType->GetDesc(&memberDesc))) {
                            continue;
                        }
                        AddField(layout, type->GetMemberTypeName(member), memberDesc,
                            variableDesc.StartOffset + memberDesc.Offset, shaderName);
                    }
                    continue;
                }
                AddField(layout, variableDesc.Name, typeDesc, variableDesc.StartOffset, shaderName);
            }
        }

        /// @brief 定数バッファの配列の要素の間隔（要素ごとに 16 バイトの区切りから始まる）
        constexpr uint32_t kArrayElementStride = 16;

        /// @brief 1 要素ぶんの数を、項目の種類に合わせて書く
        void WriteElement(std::vector<uint8_t>& out, const MaterialParamField& field, uint32_t offset,
            const MaterialNumbers& numbers)
        {
            const size_t count = (std::min)(static_cast<size_t>(field.components), numbers.size());
            for (size_t component = 0; component < count; ++component) {
                const size_t at = offset + component * sizeof(float);
                if (at + sizeof(float) > out.size()) {
                    return;
                }
                const float number = numbers[component];
                switch (field.kind) {
                case MaterialParamField::Kind::Float:
                    std::memcpy(out.data() + at, &number, sizeof(float));
                    break;
                case MaterialParamField::Kind::Int: {
                    const int32_t value = static_cast<int32_t>(std::lround(number));
                    std::memcpy(out.data() + at, &value, sizeof(value));
                    break;
                }
                case MaterialParamField::Kind::Uint: {
                    const uint32_t value = number <= 0.0f ? 0u : static_cast<uint32_t>(std::lround(number));
                    std::memcpy(out.data() + at, &value, sizeof(value));
                    break;
                }
                case MaterialParamField::Kind::Bool: {
                    const uint32_t value = number != 0.0f ? 1u : 0u;
                    std::memcpy(out.data() + at, &value, sizeof(value));
                    break;
                }
                }
            }
        }
    }

    void ReadMaterialParamLayout(IDxcUtils* utils, IDxcBlob* blob, MaterialParamLayout& layout, const std::string& shaderName)
    {
        if (!utils || !blob) {
            return;
        }
        DxcBuffer buffer{};
        buffer.Ptr = blob->GetBufferPointer();
        buffer.Size = blob->GetBufferSize();
        buffer.Encoding = 0;

        Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
        if (FAILED(utils->CreateReflection(&buffer, IID_PPV_ARGS(reflection.GetAddressOf())))) {
            return;
        }
        D3D12_SHADER_DESC shaderDesc{};
        reflection->GetDesc(&shaderDesc);

        for (UINT index = 0; index < shaderDesc.BoundResources; ++index) {
            D3D12_SHADER_INPUT_BIND_DESC bind{};
            if (FAILED(reflection->GetResourceBindingDesc(index, &bind)) || !bind.Name) {
                continue;
            }
            if (bind.Type == D3D_SIT_CBUFFER && bind.BindPoint == 0 && bind.Space == 0 && layout.size == 0) {
                ReadConstantBuffer(reflection.Get(), bind.Name, layout, shaderName);
            } else if (bind.Type == D3D_SIT_TEXTURE && std::string_view(bind.Name) != kMainTextureName) {
                if (std::find(layout.textures.begin(), layout.textures.end(), bind.Name) == layout.textures.end()) {
                    layout.textures.emplace_back(bind.Name);
                }
            }
        }
    }

    void PackMaterialParams(const MaterialParamLayout& layout, const MaterialValueLookup& find, std::vector<uint8_t>& out)
    {
        out.assign(layout.size, 0);
        for (const MaterialParamField& field : layout.fields) {
            if (field.elements == 0) {
                if (const MaterialNumbers* numbers = find(field.name)) {
                    WriteElement(out, field, field.offset, *numbers);
                }
                continue;
            }
            for (uint32_t index = 0; index < field.elements; ++index) {
                const std::string key = field.name + "[" + std::to_string(index) + "]";
                if (const MaterialNumbers* numbers = find(key)) {
                    WriteElement(out, field, field.offset + index * kArrayElementStride, *numbers);
                }
            }
        }
    }

    const MaterialParamField* MaterialParamLayout::Find(std::string_view name) const
    {
        for (const MaterialParamField& field : fields) {
            if (field.name == name) {
                return &field;
            }
        }
        return nullptr;
    }

    bool ShaderMaterialPipeline::Build(ID3D12Device* device, ShaderCompiler& compiler,
        ShaderReflectionBuilder& reflectionBuilder, const std::string& pixelShaderPath,
        const std::string& vertexShaderPath, const Target& target)
    {
        Logger& log = Logger::GetInstance();
        const std::string vertexPath = vertexShaderPath.empty() ? std::string(kStandardMaterialVertexShader) : vertexShaderPath;

        const Microsoft::WRL::ComPtr<IDxcBlob> vertexBlob =
            compiler.CompileShader(log.Utf8ToPath(vertexPath).wstring(), L"vs_6_0");
        const Microsoft::WRL::ComPtr<IDxcBlob> pixelBlob =
            compiler.CompileShader(log.Utf8ToPath(pixelShaderPath).wstring(), L"ps_6_0");
        if (!vertexBlob || !pixelBlob) {
            log.Logf(LogLevel::Error, LogCategory::Shader, "マテリアルのシェーダ {} をコンパイルできません",
                vertexBlob ? pixelShaderPath : vertexPath);
            return false;
        }

        const std::string shaderName = std::string(target.debugName) + " " + pixelShaderPath;
        const std::unique_ptr<ShaderReflectionData> reflection =
            reflectionBuilder.BuildFromShaders(vertexBlob.Get(), pixelBlob.Get(), shaderName);
        if (!reflection) {
            return false;
        }

        RootSignatureConfig config;
        config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
        config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);
        config.SetDefaultSamplerStrategy(BindingStrategy::StaticSampler);
        config.ConfigureSampler("gSampler", SamplerConfig::Linear());
        config.ConfigureSampler("gPointSampler", SamplerConfig::Point());
        config.ConfigureSampler("gClampSampler", SamplerConfig::LinearClamp());

        auto rootSignature = std::make_unique<RootSignatureManager>();
        const auto rootResult = rootSignature->Build(device, *reflection, config);
        if (!rootResult.success) {
            log.Logf(LogLevel::Error, LogCategory::Shader, "マテリアルのシェーダ {} のルートシグネチャを作れません: {}",
                pixelShaderPath, rootResult.errorMessage);
            return false;
        }

        auto pipelineStates = std::make_unique<PipelineStateManager>();
        const bool built = pipelineStates->CreateBuilder()
            .SetDebugName(target.debugName)
            .SetInputLayoutFromReflection(*reflection)
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(target.depthTest, false)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, vertexBlob.Get(), pixelBlob.Get(), rootSignature->GetRootSignature());
        if (!built) {
            log.Logf(LogLevel::Error, LogCategory::Shader, "マテリアルのシェーダ {} のパイプラインを作れません",
                pixelShaderPath);
            return false;
        }

        // 値の並びはピクセルシェーダを先に見る（頂点シェーダだけが使う値もある）
        MaterialParamLayout layout;
        ReadMaterialParamLayout(compiler.GetDxcUtils(), pixelBlob.Get(), layout, shaderName);
        ReadMaterialParamLayout(compiler.GetDxcUtils(), vertexBlob.Get(), layout, shaderName);

        rootSignature_ = std::move(rootSignature);
        pipelineStates_ = std::move(pipelineStates);
        layout_ = std::move(layout);
        transformSlot_ = rootSignature_->GetRootSlot("gSpriteTransform");
        drawSlot_ = rootSignature_->GetRootSlot("gSpriteDraw");
        textureSlot_ = rootSignature_->GetRootSlot(kMainTextureName);
        paramsSlot_ = layout_.bufferName.empty() ? RootSlot{} : rootSignature_->GetRootSlot(layout_.bufferName);
        extraTextureSlots_.clear();
        for (const std::string& name : layout_.textures) {
            extraTextureSlots_.push_back(rootSignature_->GetRootSlot(name));
        }
        return true;
    }

    ID3D12PipelineState* ShaderMaterialPipeline::GetPipelineState(BlendMode mode) const
    {
        return pipelineStates_ ? pipelineStates_->GetPipelineState(mode) : nullptr;
    }

    ID3D12RootSignature* ShaderMaterialPipeline::GetRootSignature() const
    {
        return rootSignature_ ? rootSignature_->GetRootSignature() : nullptr;
    }

    const ShaderMaterialPipeline* ShaderMaterialPipelineCache::Get(ID3D12Device* device, ShaderCompiler& compiler,
        ShaderReflectionBuilder& reflectionBuilder, const ShaderMaterial& material)
    {
        if (material.GetShaderPath().empty()) {
            return nullptr;
        }

        const uint64_t revision = ShaderMaterialLibrary::GetInstance().GetShaderRevision();
        const std::string key = material.GetShaderPath() + "|" + material.GetVertexShaderPath();
        Entry& entry = entries_[key];
        if (entry.shaderRevision == revision) {
            return entry.pipeline.get();
        }
        entry.shaderRevision = revision;

        auto pipeline = std::make_unique<ShaderMaterialPipeline>();
        if (!pipeline->Build(device, compiler, reflectionBuilder,
                material.GetShaderPath(), material.GetVertexShaderPath(), target_)) {
            return entry.pipeline.get();
        }
        if (entry.pipeline) {
            retired_.push_back(std::move(entry.pipeline));
        }
        entry.pipeline = std::move(pipeline);
        return entry.pipeline.get();
    }
}
