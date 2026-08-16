#include "pch.h"
#include "RasterScroll.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Utility/CVar/CVar.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <cassert>


namespace CoreEngine
{
    namespace
    {
        CVar<float> cvScrollSpeed{
            "r.RasterScroll.ScrollSpeed", 1.0f,
            "走査線が流れる速さ",
            CVarRange{ 0.0f, 10.0f } };

        CVar<float> cvLineHeight{
            "r.RasterScroll.LineHeight", 10.0f,
            "1 本の走査線の高さ（ピクセル）",
            CVarRange{ 1.0f, 20.0f } };

        CVar<float> cvAmplitude{
            "r.RasterScroll.Amplitude", 0.02f,
            "横方向のずれ幅",
            CVarRange{ 0.0f, 0.2f } };

        CVar<float> cvFrequency{
            "r.RasterScroll.Frequency", 1.5f,
            "波の細かさ",
            CVarRange{ 0.1f, 5.0f } };

        CVar<float> cvDistortionStrength{
            "r.RasterScroll.DistortionStrength", 1.0f,
            "歪みの強度",
            CVarRange{ 0.0f, 3.0f } };

        CVar<bool> cvEnabled{
            "r.RasterScroll.Enabled", false,
            "ラスタースクロールを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.RasterScroll";
    }

    void RasterScroll::OnCreateConstantBuffers()
    {
        UINT rsSize = (sizeof(RasterScrollParams) + 255) & ~255;
        rasterScrollParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), rsSize);
        [[maybe_unused]] HRESULT hr = rasterScrollParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedRasterScrollParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

    }

    void RasterScroll::UpdateConstantBuffer()
    {
        if (!mappedRasterScrollParams_) {
            return;
        }
        mappedRasterScrollParams_->scrollSpeed        = cvScrollSpeed.Get();
        mappedRasterScrollParams_->lineHeight         = cvLineHeight.Get();
        mappedRasterScrollParams_->amplitude          = cvAmplitude.Get();
        mappedRasterScrollParams_->frequency          = cvFrequency.Get();
        mappedRasterScrollParams_->distortionStrength = cvDistortionStrength.Get();
        // time / lineOffset は Update が計算した実行時値
        mappedRasterScrollParams_->time       = accumulatedTime_;
        mappedRasterScrollParams_->lineOffset = accumulatedTime_ * cvScrollSpeed.Get();
    }

    void RasterScroll::PrepareFrame(const PostEffectFrameContext& ctx)
    {
        accumulatedTime_ += ctx.deltaTime;
        UpdateConstantBuffer();
    }

    void RasterScroll::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateConstantBuffer();
        UpdateScreenSizeConstants(width, height);

        auto* cmdList = directXCommon_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx = GetRootParamIndex("gTexture");
        int outputIdx  = GetRootParamIndex("gOutput");
        int rsIdx      = GetRootParamIndex("RasterScrollParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (rsIdx >= 0)      cmdList->SetComputeRootConstantBufferView(rsIdx, rasterScrollParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, GetScreenSizeCbAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void RasterScroll::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("RasterScrollParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
            accumulatedTime_ = 0.0f;
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* RasterScroll::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
