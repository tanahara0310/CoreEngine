#include "ColorGrading.h"
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
    void ColorGrading::Initialize(DirectXCommon* dxCommon)
    {
        assert(dxCommon);
        directXCommon_ = dxCommon;

        ShaderCompiler compiler;
        compiler.Initialize();
        computeShaderBlob_ = compiler.CompileShader(L"ColorGrading.CS.hlsl", L"cs_6_0");

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(compiler.GetDxcUtils());
        reflectionData_ = reflectionBuilder.BuildFromComputeShader(
            computeShaderBlob_.Get(), GetEffectName());

        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);

        rootSignatureManager_ = std::make_unique<RootSignatureManager>();
        auto buildResult = rootSignatureManager_->Build(dxCommon->GetDevice(), *reflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error("ColorGrading: Failed to create RootSignature: " + buildResult.errorMessage);
        }

        CreateComputePipeline();
        CreateConstantBuffers();
    }

    void ColorGrading::CreateComputePipeline()
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = rootSignatureManager_->GetRootSignature();
        desc.CS = { computeShaderBlob_->GetBufferPointer(), computeShaderBlob_->GetBufferSize() };

        HRESULT hr = directXCommon_->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&computePso_));
        if (FAILED(hr)) {
            throw std::runtime_error("ColorGrading: Failed to create Compute PSO");
        }
    }

    void ColorGrading::CreateConstantBuffers()
    {
        UINT cgSize = (sizeof(ColorGradingParams) + 255) & ~255;
        colorGradingParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), cgSize);
        HRESULT hr = colorGradingParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedColorGradingParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void ColorGrading::UpdateConstantBuffer()
    {
        if (mappedColorGradingParams_) { *mappedColorGradingParams_ = params_; }
    }

    void ColorGrading::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void ColorGrading::SetParams(const ColorGradingParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void ColorGrading::ApplyPreset(int presetIndex)
    {
        ColorGradingParams preset;
        switch (presetIndex) {
        case 1: // ウォーム
            preset.temperature = 0.3f;
            preset.saturation  = 1.2f;
            break;
        case 2: // クール
            preset.temperature = -0.3f;
            preset.saturation  = 0.9f;
            break;
        case 3: // ハイコントラスト
            preset.contrast = 1.5f;
            preset.exposure = 0.2f;
            break;
        default:
            break;
        }
        SetParams(preset);
    }

    void ColorGrading::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateScreenConstantBuffer(width, height);

        auto* cmdList = directXCommon_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx = GetRootParamIndex("gTexture");
        int outputIdx  = GetRootParamIndex("gOutput");
        int cgIdx      = GetRootParamIndex("ColorGradingParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (cgIdx >= 0)      cmdList->SetComputeRootConstantBufferView(cgIdx, colorGradingParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, screenParamsCB_->GetGPUVirtualAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void ColorGrading::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("ColorGrading");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        bool changed = false;
        if (ImGui::TreeNode("基本")) {
            changed |= UI::SliderFloat("色相", params_.hue, -1.0f, 1.0f);
            changed |= UI::SliderFloat("彩度", params_.saturation, 0.0f, 3.0f);
            changed |= UI::SliderFloat("明度", params_.value, 0.0f, 3.0f);
            changed |= UI::SliderFloat("コントラスト", params_.contrast, 0.0f, 3.0f);
            changed |= UI::SliderFloat("ガンマ", params_.gamma, 0.1f, 3.0f);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("カラー")) {
            changed |= UI::SliderFloat("色温度", params_.temperature, -1.0f, 1.0f);
            changed |= UI::SliderFloat("ティント", params_.tint, -1.0f, 1.0f);
            changed |= UI::SliderFloat("露出", params_.exposure, -3.0f, 3.0f);
            ImGui::TreePop();
        }
        if (changed) { UpdateConstantBuffer(); }

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            params_ = ColorGradingParams{};
            UpdateConstantBuffer();
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }
}
