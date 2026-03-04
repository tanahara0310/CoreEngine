#include "Dissolve.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/TextureManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include <cassert>

namespace CoreEngine
{
    void Dissolve::Initialize(DirectXCommon* dxCommon)
    {
        assert(dxCommon);
        directXCommon_ = dxCommon;

        // シェーダーのコンパイル
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        fullscreenVertexShaderBlob_ = shaderCompiler.CompileShader(
            L"Engine/Assets/Shaders/PostProcess/FullScreen.VS.hlsl", L"vs_6_0");
        pixelShaderBlob_ = shaderCompiler.CompileShader(
            GetPixelShaderPath(), L"ps_6_0");

        // カスタムルートシグネチャの作成
        CreateCustomRootSignature();

        // パイプラインステートの作成
        bool result = pipelineStateManager_.CreateBuilder()
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(false, false)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .Build(dxCommon->GetDevice(), fullscreenVertexShaderBlob_.Get(), pixelShaderBlob_.Get(),
                rootSignatureManager_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create PSO in Dissolve");
        }

        // 定数バッファの作成
        CreateConstantBuffer();

        // ノイズテクスチャの読み込み
        LoadNoiseTexture();
    }

    void Dissolve::CreateCustomRootSignature()
    {
        // シェーダーコンパイラを初期化してDxcUtilsを取得
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        // リフレクション
        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        reflectionData_ = reflectionBuilder.BuildFromShaders(
            fullscreenVertexShaderBlob_.Get(), pixelShaderBlob_.Get(), "Dissolve");

        // シンプルな設定でRootSignatureを構築
        RootSignatureConfig config = RootSignatureConfig::Simple();
        config.ConfigureSampler("gSampler", SamplerConfig::Linear());

        rootSignatureManager_ = std::make_unique<RootSignatureManager>();
        auto buildResult = rootSignatureManager_->Build(directXCommon_->GetDevice(), *reflectionData_, config);

        if (!buildResult.success) {
            throw std::runtime_error("Failed to create Dissolve Root Signature: " + buildResult.errorMessage);
        }
    }

    void Dissolve::Draw(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
    {
        auto* commandList = directXCommon_->GetCommandList();

        commandList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        commandList->SetPipelineState(
            pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 入力テクスチャ
        int inputTextureIdx = GetRootParamIndex("inputTexture");
        if (inputTextureIdx >= 0) {
            commandList->SetGraphicsRootDescriptorTable(inputTextureIdx, inputSrvHandle);
        }

        // 定数バッファ
        int paramsIdx = GetRootParamIndex("DissolveParams");
        if (constantBuffer_ && paramsIdx >= 0) {
            commandList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
        }

        // ノイズテクスチャ
        int noiseTextureIdx = GetRootParamIndex("noiseTexture");
        if (noiseTextureIdx >= 0) {
            commandList->SetGraphicsRootDescriptorTable(noiseTextureIdx, noiseTextureHandle_);
        }

        commandList->DrawInstanced(3, 1, 0, 0);
    }

    void Dissolve::DrawImGui()
    {
#ifdef _DEBUG
        ImGui::PushID("DissolveParams");

        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::Text("ノイズテクスチャを使用してディゾルブ効果を作成します");
        ImGui::Separator();

        bool paramsChanged = false;

        // パラメータ設定
        if (ImGui::TreeNode("パラメータ")) {
            // ディゾルブ閾値の調整
            paramsChanged |= ImGui::SliderFloat("閾値", &params_.threshold, 0.0f, 1.0f);

            // エッジ幅の調整
            paramsChanged |= ImGui::SliderFloat("エッジ幅", &params_.edgeWidth, 0.0f, 0.5f);

            // エッジカラーの調整
            float edgeColor[3] = { params_.edgeColorR, params_.edgeColorG, params_.edgeColorB };
            if (ImGui::ColorEdit3("エッジカラー", edgeColor)) {
                params_.edgeColorR = edgeColor[0];
                params_.edgeColorG = edgeColor[1];
                params_.edgeColorB = edgeColor[2];
                paramsChanged = true;
            }

            ImGui::TreePop();
        }

        // パラメータが変更された場合、即座に定数バッファを更新
        if (paramsChanged) {
            UpdateConstantBuffer();
        }

        ImGui::Separator();

        if (ImGui::Button("デフォルトに戻す")) {
            params_.threshold = 0.0f;
            params_.edgeWidth = 0.1f;
            params_.edgeColorR = 1.0f;
            params_.edgeColorG = 0.5f;
            params_.edgeColorB = 0.0f;
            UpdateConstantBuffer();
        }

        if (!IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
        }

        ImGui::Separator();

        ImGui::PopID();
#endif // _DEBUG
    }

    void Dissolve::SetParams(const DissolveParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void Dissolve::SetThreshold(float threshold)
    {
        params_.threshold = threshold;
        UpdateConstantBuffer();
    }

    void Dissolve::SetEdgeWidth(float width)
    {
        params_.edgeWidth = width;
        UpdateConstantBuffer();
    }

    void Dissolve::SetEdgeColor(float r, float g, float b)
    {
        params_.edgeColorR = r;
        params_.edgeColorG = g;
        params_.edgeColorB = b;
        UpdateConstantBuffer();
    }

    void Dissolve::ForceUpdateConstantBuffer()
    {
        UpdateConstantBuffer();
    }

    void Dissolve::BindOptionalCBVs(ID3D12GraphicsCommandList* /*commandList*/)
    {
        // この関数はDissolveでは使用しない（Drawで直接バインドしているため）
    }

    void Dissolve::UpdateConstantBuffer()
    {
        // 定数バッファにデータをコピー
        if (mappedData_) {
            *mappedData_ = params_;
        }
    }

    void Dissolve::CreateConstantBuffer()
    {
        assert(directXCommon_);

        // 定数バッファのサイズを256バイトアライメントに調整
        UINT bufferSize = (sizeof(DissolveParams) + 255) & ~255;

        // ヒーププロパティ
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        // リソースデスク
        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = bufferSize;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        // リソースの作成
        HRESULT hr = directXCommon_->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&constantBuffer_)
        );
        assert(SUCCEEDED(hr));

        // マッピング
        hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
        assert(SUCCEEDED(hr));

        // 初期値で更新
        UpdateConstantBuffer();
    }

    void Dissolve::LoadNoiseTexture()
    {
        // ノイズテクスチャを読み込み
        auto& textureManager = TextureManager::GetInstance();
        auto texture = textureManager.Load("Application/Assets/Texture/noise0.png");
        noiseTextureHandle_ = texture.gpuHandle;
    }
}

