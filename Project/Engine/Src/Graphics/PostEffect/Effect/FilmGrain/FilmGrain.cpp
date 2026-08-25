#include "pch.h"
#include "FilmGrain.h"
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
        // 既定値の根拠:
        //   実写の粒は「言われて初めて気付く」程度が正解で、見えたら強すぎる。
        //   0.03 は 255 階調で ±7.6 相当。平坦な面の質感には十分で、
        //   ディテールのある領域では元の模様に埋もれる。
        CVar<float> cvIntensity{
            "r.FilmGrain.Intensity", 0.030f,
            "グレインの全体強度。0 で無効",
            CVarRange{ 0.0f, 0.2f } };

        // シャドウで強くハイライトで弱いのが銀塩の挙動。一様にすると
        // 明るい面が汚れて見え、一気に安っぽくなる
        CVar<float> cvIntensityShadows{
            "r.FilmGrain.IntensityShadows", 1.0f,
            "シャドウ帯の強度倍率",
            CVarRange{ 0.0f, 2.0f } };

        CVar<float> cvIntensityMidtones{
            "r.FilmGrain.IntensityMidtones", 0.7f,
            "中間調帯の強度倍率",
            CVarRange{ 0.0f, 2.0f } };

        CVar<float> cvIntensityHighlights{
            "r.FilmGrain.IntensityHighlights", 0.2f,
            "ハイライト帯の強度倍率。実フィルムは飽和側で粒が目立たない",
            CVarRange{ 0.0f, 2.0f } };

        CVar<float> cvGrainSize{
            "r.FilmGrain.GrainSize", 1.0f,
            "粒の大きさ[px]。1 で 1 ピクセル",
            CVarRange{ 1.0f, 4.0f } };

        // 色ノイズが強いとフィルムでなくデジタルのセンサーノイズに見える
        CVar<float> cvChromaAmount{
            "r.FilmGrain.ChromaAmount", 0.10f,
            "色ノイズの割合。0 でモノクロ粒",
            CVarRange{ 0.0f, 1.0f } };

        // 既定は無効。60fps で毎フレーム粒を差し替えると「ノイズが走る」ように見えて
        // 目に障る（映画の粒が気にならないのは 24fps かつ粒が細かいため）。
        // 使う場合は Intensity を 0.015 以下から試し、必要なら粒の更新頻度も落とすこと。
        CVar<bool> cvEnabled{
            "r.FilmGrain.Enabled", false,
            "フィルムグレインを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.FilmGrain";
    }

    void FilmGrain::OnCreateConstantBuffers()
    {
        UINT size = (sizeof(FilmGrainParams) + 255) & ~255;
        filmGrainParamsCB_ = ResourceFactory::CreateBufferResource(graphicsCore_->GetDevice(), size);
        [[maybe_unused]] HRESULT hr =
            filmGrainParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedFilmGrainParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();
    }

    void FilmGrain::UpdateConstantBuffer()
    {
        if (!mappedFilmGrainParams_) {
            return;
        }
        mappedFilmGrainParams_->intensity           = cvIntensity.Get();
        mappedFilmGrainParams_->intensityShadows    = cvIntensityShadows.Get();
        mappedFilmGrainParams_->intensityMidtones   = cvIntensityMidtones.Get();
        mappedFilmGrainParams_->intensityHighlights = cvIntensityHighlights.Get();
        mappedFilmGrainParams_->grainSize           = cvGrainSize.Get();
        mappedFilmGrainParams_->chromaAmount        = cvChromaAmount.Get();
        mappedFilmGrainParams_->time                = elapsedTime_;
    }

    void FilmGrain::PrepareFrame(const PostEffectFrameContext& ctx)
    {
        // 粒が静止していると「汚れ」に見える。毎フレーム散らす
        elapsedTime_ += ctx.deltaTime;
        // float 精度が落ちる前に巻き戻す（粒のパターンは周期性を持たないので継ぎ目は出ない）
        if (elapsedTime_ > 3600.0f) {
            elapsedTime_ = 0.0f;
        }
    }

    void FilmGrain::Dispatch(
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
        int grainIdx   = GetRootParamIndex("FilmGrainParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (grainIdx >= 0)   cmdList->SetComputeRootConstantBufferView(grainIdx, filmGrainParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, GetScreenSizeCbAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void FilmGrain::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("FilmGrainParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::TextDisabled("常時薄く乗せる想定。見えたら強すぎる");
        UI::Separator();

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* FilmGrain::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
