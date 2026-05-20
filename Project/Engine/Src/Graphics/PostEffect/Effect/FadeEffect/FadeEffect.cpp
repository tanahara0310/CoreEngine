#include "FadeEffect.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>
#include <stdexcept>
#include <algorithm>


namespace CoreEngine
{
    void FadeEffect::Initialize(DirectXCommon* dxCommon)
    {
        assert(dxCommon);
        directXCommon_ = dxCommon;

        ShaderCompiler compiler;
        compiler.Initialize();
        computeShaderBlob_ = compiler.CompileShader(L"FadeEffect.CS.hlsl", L"cs_6_0");

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(compiler.GetDxcUtils());
        reflectionData_ = reflectionBuilder.BuildFromComputeShader(
            computeShaderBlob_.Get(), GetEffectName());

        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);

        rootSignatureManager_ = std::make_unique<RootSignatureManager>();
        auto buildResult = rootSignatureManager_->Build(dxCommon->GetDevice(), *reflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error("FadeEffect: Failed to create RootSignature: " + buildResult.errorMessage);
        }

        CreateComputePipeline();
        CreateConstantBuffers();
    }

    void FadeEffect::CreateComputePipeline()
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = rootSignatureManager_->GetRootSignature();
        desc.CS = { computeShaderBlob_->GetBufferPointer(), computeShaderBlob_->GetBufferSize() };

        HRESULT hr = directXCommon_->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&computePso_));
        if (FAILED(hr)) {
            throw std::runtime_error("FadeEffect: Failed to create Compute PSO");
        }
    }

    void FadeEffect::CreateConstantBuffers()
    {
        UINT fadeSize = (sizeof(FadeParams) + 255) & ~255;
        fadeParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), fadeSize);
        HRESULT hr = fadeParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedFadeParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void FadeEffect::UpdateConstantBuffer()
    {
        if (mappedFadeParams_) { *mappedFadeParams_ = params_; }
    }

    void FadeEffect::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void FadeEffect::Update(float deltaTime)
    {
        timeAccumulator_  += deltaTime;
        params_.time       = timeAccumulator_;
        UpdateConstantBuffer();
    }

    void FadeEffect::SetFadeAlpha(float alpha)
    {
        params_.fadeAlpha = std::clamp(alpha, 0.0f, 1.0f);
        UpdateConstantBuffer();
    }

    void FadeEffect::SetFadeType(bool fadeToBlack)
    {
        params_.fadeType = fadeToBlack ? 0.0f : 1.0f;
        UpdateConstantBuffer();
    }

    void FadeEffect::SetFadeType(FadeType type)
    {
        params_.fadeType = static_cast<float>(type);
        UpdateConstantBuffer();
    }

    void FadeEffect::SetSpiralPower(float power)
    {
        params_.spiralPower = power;
        UpdateConstantBuffer();
    }

    void FadeEffect::SetRippleFrequency(float frequency)
    {
        params_.rippleFreq = frequency;
        UpdateConstantBuffer();
    }

    void FadeEffect::SetGlitchIntensity(float intensity)
    {
        params_.glitchIntensity = intensity;
        UpdateConstantBuffer();
    }

    void FadeEffect::SetPortalSize(float size)
    {
        params_.portalSize = size;
        UpdateConstantBuffer();
    }

    void FadeEffect::SetColorShift(float shift)
    {
        params_.colorShift = shift;
        UpdateConstantBuffer();
    }

    void FadeEffect::Dispatch(
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
        int fadeIdx    = GetRootParamIndex("FadeParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (fadeIdx >= 0)    cmdList->SetComputeRootConstantBufferView(fadeIdx, fadeParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, screenParamsCB_->GetGPUVirtualAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void FadeEffect::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("FadeEffect");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        const char* typeNames[] = { "黒フェード", "白フェード", "渦巻きフェード", "波紋フェード", "グリッチフェード", "ポータルフェード" };
        int currentType = static_cast<int>(params_.fadeType);
        if (ImGui::Combo("フェードタイプ", &currentType, typeNames, 6)) {
            params_.fadeType = static_cast<float>(currentType);
            UpdateConstantBuffer();
        }

        bool changed = false;
        if (ImGui::TreeNode("パラメータ")) {
            changed |= UI::SliderFloat("フェード強度", params_.fadeAlpha, 0.0f, 1.0f);
            changed |= UI::SliderFloat("渦巻き強度", params_.spiralPower, 1.0f, 20.0f);
            changed |= UI::SliderFloat("波紋周波数", params_.rippleFreq, 1.0f, 30.0f);
            changed |= UI::SliderFloat("グリッチ強度", params_.glitchIntensity, 0.0f, 1.0f);
            changed |= UI::SliderFloat("ポータルサイズ", params_.portalSize, 0.0f, 1.0f);
            changed |= UI::SliderFloat("色相シフト", params_.colorShift, 0.0f, 1.0f);
            ImGui::TreePop();
        }
        if (changed) { UpdateConstantBuffer(); }

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            params_ = FadeParams{};
            timeAccumulator_ = 0.0f;
            UpdateConstantBuffer();
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }
}
