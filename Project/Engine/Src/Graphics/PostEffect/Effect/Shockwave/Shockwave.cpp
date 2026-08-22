#include "pch.h"
#include "Shockwave.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/CVar/CVar.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <cassert>


namespace CoreEngine
{
    namespace
    {
        CVar<float> cvStrength{
            "r.Shockwave.Strength", 0.1f,
            "衝撃波による歪みの強さ",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvThickness{
            "r.Shockwave.Thickness", 0.1f,
            "波のリングの厚み",
            CVarRange{ 0.01f, 0.5f } };

        CVar<float> cvSpeed{
            "r.Shockwave.Speed", 1.0f,
            "波が広がる速さ",
            CVarRange{ 0.1f, 5.0f } };

        CVar<bool> cvEnabled{
            "r.Shockwave.Enabled", false,
            "ショックウェーブを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.Shockwave";
    }

    void Shockwave::OnCreateConstantBuffers()
    {
        UINT swSize = (sizeof(ShockwaveParams) + 255) & ~255;
        shockwaveParamsCB_ = ResourceFactory::CreateBufferResource(graphicsCore_->GetDevice(), swSize);
        [[maybe_unused]] HRESULT hr = shockwaveParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedShockwaveParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

    }

    void Shockwave::UpdateConstantBuffer()
    {
        if (!mappedShockwaveParams_) {
            return;
        }
        mappedShockwaveParams_->strength  = cvStrength.Get();
        mappedShockwaveParams_->thickness = cvThickness.Get();
        mappedShockwaveParams_->speed     = cvSpeed.Get();
        // 発動状態は実行時の値
        mappedShockwaveParams_->center[0] = centerX_;
        mappedShockwaveParams_->center[1] = centerY_;
        mappedShockwaveParams_->time      = time_;
    }

    void Shockwave::StartShockwave(float centerX, float centerY)
    {
        centerX_  = centerX;
        centerY_  = centerY;
        time_     = 0.0f;
        isActive_ = true;
        UpdateConstantBuffer();
    }

    void Shockwave::PrepareFrame(const PostEffectFrameContext& ctx)
    {
        if (!isActive_) { return; }

        time_ += ctx.deltaTime * cvSpeed.Get();
        if (time_ >= maxRadius_) {
            isActive_ = false;
            time_     = 0.0f;
        }
        UpdateConstantBuffer();
    }

    void Shockwave::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateConstantBuffer();
        UpdateScreenSizeConstants(width, height);

        auto* cmdList = graphicsCore_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx = GetRootParamIndex("gTexture");
        int outputIdx  = GetRootParamIndex("gOutput");
        int swIdx      = GetRootParamIndex("ShockwaveParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (swIdx >= 0)      cmdList->SetComputeRootConstantBufferView(swIdx, shockwaveParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, GetScreenSizeCbAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void Shockwave::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("ShockwaveParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::Text("アクティブ: %s", isActive_ ? "true" : "false");
        UI::Separator();

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("ショックウェーブ発動")) {
            StartShockwave(0.5f, 0.5f);
        }
        ImGui::SameLine();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* Shockwave::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
