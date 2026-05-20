#include "Blur.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>
#include <stdexcept>


namespace CoreEngine
{
    void Blur::Initialize(DirectXCommon* dxCommon)
    {
        assert(dxCommon);
        directXCommon_ = dxCommon;

        // CSのみコンパイル（VS/PSは不要）
        ShaderCompiler compiler;
        compiler.Initialize();
        computeShaderBlob_ = compiler.CompileShader(L"Blur.CS.hlsl", L"cs_6_0");

        // シェーダーリフレクションからルートシグネチャを構築
        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(compiler.GetDxcUtils());
        reflectionData_ = reflectionBuilder.BuildFromComputeShader(
            computeShaderBlob_.Get(), GetEffectName());

        RootSignatureConfig config;
        // コンピュートシェーダーはIA不要
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);

        rootSignatureManager_ = std::make_unique<RootSignatureManager>();
        auto buildResult = rootSignatureManager_->Build(dxCommon->GetDevice(), *reflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error("Blur: Failed to create RootSignature: " + buildResult.errorMessage);
        }

        // Compute PSO を構築
        CreateComputePipeline();

        // 定数バッファを作成
        CreateConstantBuffers();
    }

    void Blur::CreateComputePipeline()
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = rootSignatureManager_->GetRootSignature();
        desc.CS = { computeShaderBlob_->GetBufferPointer(), computeShaderBlob_->GetBufferSize() };

        HRESULT hr = directXCommon_->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&computePso_));
        if (FAILED(hr)) {
            throw std::runtime_error("Blur: Failed to create Compute PSO");
        }
    }

    void Blur::CreateConstantBuffers()
    {
        // BlurParams 定数バッファ
        UINT blurSize = (sizeof(BlurParams) + 255) & ~255;
        blurParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), blurSize);
        HRESULT hr = blurParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedBlurParams_));
        assert(SUCCEEDED(hr));
        UpdateBlurConstantBuffer();

        // ScreenParams 定数バッファ
        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void Blur::UpdateBlurConstantBuffer()
    {
        if (mappedBlurParams_) {
            *mappedBlurParams_ = params_;
        }
    }

    void Blur::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void Blur::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        // 画面サイズ定数バッファを更新
        UpdateScreenConstantBuffer(width, height);

        auto* cmdList = directXCommon_->GetCommandList();

        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        // リソースをバインド（リフレクションでインデックスを取得）
        int textureIdx      = GetRootParamIndex("gTexture");
        int outputIdx       = GetRootParamIndex("gOutput");
        int blurParamsIdx   = GetRootParamIndex("BlurParams");
        int screenParamsIdx = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) {
            cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        }
        if (outputIdx >= 0) {
            cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        }
        if (blurParamsIdx >= 0) {
            cmdList->SetComputeRootConstantBufferView(blurParamsIdx, blurParamsCB_->GetGPUVirtualAddress());
        }
        if (screenParamsIdx >= 0) {
            cmdList->SetComputeRootConstantBufferView(screenParamsIdx, screenParamsCB_->GetGPUVirtualAddress());
        }

        // スレッドグループ数 = ceil(解像度 / 8)
        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void Blur::SetParams(const BlurParams& params)
    {
        params_ = params;
        UpdateBlurConstantBuffer();
    }

    void Blur::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("BlurParams");

        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        bool paramsChanged = false;

        if (ImGui::TreeNode("パラメータ")) {
            paramsChanged |= UI::SliderFloat("強度", params_.intensity, 0.0f, 5.0f);
            paramsChanged |= UI::SliderFloat("カーネルサイズ", params_.kernelSize, 0.5f, 3.0f);
            ImGui::TreePop();
        }

        if (paramsChanged) {
            UpdateBlurConstantBuffer();
        }

        UI::Separator();

        if (ImGui::Button("デフォルトに戻す")) {
            params_.intensity  = 1.0f;
            params_.kernelSize = 1.0f;
            UpdateBlurConstantBuffer();
        }

        if (!IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
        }

        ImGui::PopID();
#endif // USE_IMGUI
    }
}

