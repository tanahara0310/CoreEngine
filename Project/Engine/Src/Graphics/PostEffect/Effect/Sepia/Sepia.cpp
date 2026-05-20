#include "Sepia.h"
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
    void Sepia::Initialize(DirectXCommon* dxCommon)
    {
        assert(dxCommon);
        directXCommon_ = dxCommon;

        ShaderCompiler compiler;
        compiler.Initialize();
        computeShaderBlob_ = compiler.CompileShader(L"Sepia.CS.hlsl", L"cs_6_0");

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(compiler.GetDxcUtils());
        reflectionData_ = reflectionBuilder.BuildFromComputeShader(
            computeShaderBlob_.Get(), GetEffectName());

        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);

        rootSignatureManager_ = std::make_unique<RootSignatureManager>();
        auto buildResult = rootSignatureManager_->Build(dxCommon->GetDevice(), *reflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error("Sepia: Failed to create RootSignature: " + buildResult.errorMessage);
        }

        CreateComputePipeline();
        CreateConstantBuffers();
    }

    void Sepia::CreateComputePipeline()
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = rootSignatureManager_->GetRootSignature();
        desc.CS = { computeShaderBlob_->GetBufferPointer(), computeShaderBlob_->GetBufferSize() };

        HRESULT hr = directXCommon_->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&computePso_));
        if (FAILED(hr)) {
            throw std::runtime_error("Sepia: Failed to create Compute PSO");
        }
    }

    void Sepia::CreateConstantBuffers()
    {
        // SepiaParams 定数バッファ
        UINT sepiaSize = (sizeof(SepiaParams) + 255) & ~255;
        sepiaParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), sepiaSize);
        HRESULT hr = sepiaParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedSepiaParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        // ScreenParams 定数バッファ
        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void Sepia::UpdateConstantBuffer()
    {
        if (mappedSepiaParams_) {
            *mappedSepiaParams_ = params_;
        }
    }

    void Sepia::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void Sepia::SetParams(const SepiaParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void Sepia::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateScreenConstantBuffer(width, height);

        auto* cmdList = directXCommon_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx      = GetRootParamIndex("gTexture");
        int outputIdx       = GetRootParamIndex("gOutput");
        int sepiaParamsIdx  = GetRootParamIndex("SepiaParams");
        int screenParamsIdx = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0)      cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)       cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (sepiaParamsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(sepiaParamsIdx, sepiaParamsCB_->GetGPUVirtualAddress());
        if (screenParamsIdx >= 0) cmdList->SetComputeRootConstantBufferView(screenParamsIdx, screenParamsCB_->GetGPUVirtualAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void Sepia::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("SepiaParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        bool changed = false;
        if (ImGui::TreeNode("パラメータ")) {
            changed |= UI::SliderFloat("強度", params_.intensity, 0.0f, 2.0f);
            changed |= UI::SliderFloat("赤色調整", params_.toneRed, 0.5f, 1.5f);
            changed |= UI::SliderFloat("緑色調整", params_.toneGreen, 0.5f, 1.5f);
            changed |= UI::SliderFloat("青色調整", params_.toneBlue, 0.5f, 1.5f);
            ImGui::TreePop();
        }
        if (changed) { UpdateConstantBuffer(); }

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            params_ = SepiaParams{};
            UpdateConstantBuffer();
        }
        if (!IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }
}
