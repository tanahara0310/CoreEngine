#include "pch.h"
#include "ProjectLoadingScreen.h"
#include "Editor/ImGui/ImguiManager.h"
#include "EngineSystem/Settings/ProjectSettings.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Texture/TextureManager.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace CoreEngine
{
    namespace
    {
        // 有効・無効は SceneTransition が遷移のたびに切り替える（保存しない）
        CVar<bool> cvEnabled{
            "r.ProjectLoading.Enabled", false,
            "プロジェクトのロード画面を有効にする（通常は SceneTransition が自動で切り替える）",
            CVarRange{}, CVarFlags::NoSave | CVarFlags::NoUI };

        // 1 フレームで進める時間の上限
        constexpr float kMaxDeltaSeconds = 0.1f;
        // 時間はこの秒数で一周させる（float の精度を保つ）
        constexpr float kTimeWrapSeconds = 1000.0f;
        // 表示する進み具合が読み込みの進み具合へ寄る速さ（1/秒）
        constexpr float kProgressFollowRate = 6.0f;
        // 画像の無い枠に入れる白い 1×1
        constexpr const char* kFallbackImage = "white1x1.png";
    }

    bool ProjectLoadingScreen::IsConfigured()
    {
        return !ProjectSettings::Get().GetLoadingScreenShader().empty();
    }

    std::wstring ProjectLoadingScreen::GetComputeShaderPath() const
    {
        return Logger::GetInstance().Utf8ToPath(ProjectSettings::Get().GetLoadingScreenShader()).wstring();
    }

    void ProjectLoadingScreen::OnCreateResources()
    {
        auto& textureManager = TextureManager::GetInstance();
        const std::vector<std::string>& paths = ProjectSettings::Get().GetLoadingScreenImages();
        const D3D12_GPU_DESCRIPTOR_HANDLE fallback = textureManager.Load(kFallbackImage).gpuHandle;
        for (size_t i = 0; i < kMaxImages; ++i) {
            images_[i] = i < paths.size() ? textureManager.Load(paths[i]).gpuHandle : fallback;
            if (images_[i].ptr == 0) {
                images_[i] = fallback;
            }
        }
        if (paths.size() > kMaxImages) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "ロード画面の画像は {} 枚まで使えます（{} 枚目からは渡しません）", kMaxImages, kMaxImages + 1);
        }
    }

    void ProjectLoadingScreen::PrepareFrame(const PostEffectFrameContext& ctx)
    {
        const float deltaTime = std::min(ctx.deltaTime, kMaxDeltaSeconds);
        time_ = std::fmod(time_ + deltaTime, kTimeWrapSeconds);
        realTime_ = std::fmod(realTime_ + Time::UnscaledDeltaTime(), kTimeWrapSeconds);
        progress_ += (progressTarget_ - progress_) * (1.0f - std::exp(-kProgressFollowRate * deltaTime));
    }

    void ProjectLoadingScreen::SetScreenAlpha(float alpha)
    {
        const float next = std::clamp(alpha, 0.0f, 1.0f);
        // 出し始めるたびに時間と進み具合を頭から数える
        if (screenAlpha_ <= 0.0f && next > 0.0f) {
            time_ = 0.0f;
            realTime_ = 0.0f;
            progress_ = 0.0f;
            progressTarget_ = 0.0f;
        }
        screenAlpha_ = next;
    }

    void ProjectLoadingScreen::SetProgress(float progress)
    {
        progressTarget_ = std::clamp(progress, 0.0f, 1.0f);
    }

    void ProjectLoadingScreen::SetGaugeAlpha(float alpha)
    {
        gaugeAlpha_ = std::clamp(alpha, 0.0f, 1.0f);
    }

    void ProjectLoadingScreen::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateScreenSizeConstants(width, height);

        ProjectLoadingParams params{};
        params.screenAlpha = screenAlpha_;
        params.time = time_;
        params.realTime = realTime_;
        params.progress = progress_;
        params.gaugeAlpha = gaugeAlpha_;

        auto* cmdList = graphicsCore_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        const int textureIdx = GetRootParamIndex("gTexture");
        const int outputIdx = GetRootParamIndex("gOutput");
        const int paramsIdx = GetRootParamIndex("ProjectLoadingParams");
        const int screenIdx = GetRootParamIndex("ScreenParams");
        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (paramsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(paramsIdx, UploadConstants(params));
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, GetScreenSizeCbAddress());
        for (size_t i = 0; i < kMaxImages; ++i) {
            const int imageIdx = GetRootParamIndex("gImage" + std::to_string(i));
            if (imageIdx >= 0) {
                cmdList->SetComputeRootDescriptorTable(imageIdx, images_[i]);
            }
        }

        const uint32_t groupX = (width + 7) / 8;
        const uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void ProjectLoadingScreen::DrawImGui()
    {
#ifdef CORE_EDITOR
        ImGui::PushID("ProjectLoadingScreen");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::TextWrapped("シェーダ: %s", ProjectSettings::Get().GetLoadingScreenShader().c_str());
        UI::Separator();
        // シーン遷移が決める表示強度と進み具合を、ここで直接動かして見る
        UI::SliderFloat("表示強度（実行時）", screenAlpha_, 0.0f, 1.0f);
        if (UI::SliderFloat("進み具合（実行時）", progressTarget_, 0.0f, 1.0f)) {
            progress_ = progressTarget_;
        }
        ImGui::PopID();
#endif // CORE_EDITOR
    }

    CVar<bool>* ProjectLoadingScreen::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
