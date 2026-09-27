#include "pch.h"
#include "Editor/Scene/SceneResaveDialog.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/PlaybackState.h"
#include "EngineSystem/Settings/EditorSettingsSubsystem.h"
#include "Scene/SceneManager.h"
#include "Scene/SceneSaveSystem.h"
#include "Utility/Logger/Logger.h"

#include <imgui.h>

#include <Windows.h>

#include <algorithm>
#include <cfloat>
#include <format>
#include <utility>

namespace CoreEngine::Editor
{
    namespace
    {
        constexpr const char* kTitle = "すべてのシーンと設定を保存し直す";

        /// 開き終わってから保存するまでに回すフレーム数（最初の描画で決まる値を入れてから保存する）
        constexpr int kSettleFrames = 30;

        /// 開き終わるのを待つ長さ（過ぎたら開けなかったものとして次へ進む）
        constexpr std::chrono::seconds kGiveUpTime{ 120 };

        /// @brief ボタンの幅
        float ButtonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        }
    }

    void SceneResaveDialog::Open()
    {
        if (state_ == State::Running) {
            return;
        }
        state_ = State::Ready;
        requestOpen_ = true;
    }

    void SceneResaveDialog::StartAndQuit()
    {
        quitWhenDone_ = true;
        startWhenReady_ = true;
    }

    void SceneResaveDialog::Start(SceneManager& scenes)
    {
        scenes_.clear();
        for (const std::string& name : SceneSaveSystem::ListSavedScenes()) {
            if (scenes.HasScene(name)) {
                scenes_.push_back(name);
            }
        }
        next_ = 0;
        returnTo_ = scenes.GetCurrentSceneName();
        waitingFor_.clear();
        savedCount_ = 0;
        settingsCount_ = 0;
        failed_.clear();
        state_ = State::Running;
        requestOpen_ = true;

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "SceneResaveDialog: {} 個のシーンを保存し直します", scenes_.size());
    }

    void SceneResaveDialog::OpenScene(SceneManager& scenes, const std::string& name, bool save)
    {
        waitingFor_ = name;
        saveWhenOpened_ = save;
        settledFrames_ = 0;
        waitStarted_ = std::chrono::steady_clock::now();
        scenes.ChangeScene(name, SceneTransition::TransitionType::None, 0.0f);
    }

    void SceneResaveDialog::Step(SceneManager& scenes, SceneDebugEditor* editor, EditorSettingsSubsystem* settings)
    {
        Logger& log = Logger::GetInstance();

        // 開いたシーンが落ち着いたら保存する
        if (!waitingFor_.empty()) {
            const bool opened = scenes.CanLoadSceneNow() && scenes.GetCurrentSceneName() == waitingFor_
                && editor && editor->GetSceneName() == waitingFor_;
            if (opened) {
                if (++settledFrames_ < kSettleFrames) {
                    return;
                }
                if (saveWhenOpened_ && editor->SaveScene()) {
                    ++savedCount_;
                    log.Logf(LogLevel::Info, LogCategory::System, "SceneResaveDialog: {} を保存しました", waitingFor_);
                }
                waitingFor_.clear();
            } else {
                settledFrames_ = 0;
                if (std::chrono::steady_clock::now() - waitStarted_ < kGiveUpTime) {
                    return;
                }
                log.Logf(LogLevel::Error, LogCategory::System, "SceneResaveDialog: {} を開けませんでした", waitingFor_);
                failed_.push_back(std::exchange(waitingFor_, {}));
            }
        }

        // 次のシーンを開く。全部済んだら元のシーンへ戻る
        if (next_ < scenes_.size()) {
            OpenScene(scenes, scenes_[next_++], true);
            return;
        }
        if (!returnTo_.empty()) {
            OpenScene(scenes, std::exchange(returnTo_, {}), false);
            return;
        }

        settingsCount_ = settings ? settings->RewriteAll() : 0;
        state_ = State::Done;
        log.Logf(LogLevel::Info, LogCategory::System,
            "SceneResaveDialog: シーン {} 個と設定のファイル {} 個を保存し直しました（開けなかったシーン {} 個）",
            savedCount_, settingsCount_, failed_.size());

        if (quitWhenDone_) {
            ::PostQuitMessage(0);
        }
    }

    void SceneResaveDialog::Draw(SceneManager* scenes, SceneDebugEditor* editor, EditorSettingsSubsystem* settings)
    {
        if (startWhenReady_ && scenes && editor && scenes->CanLoadSceneNow()
            && PlaybackStateManager::GetInstance().IsEditing()) {
            startWhenReady_ = false;
            Start(*scenes);
        }
        if (state_ == State::Running && scenes) {
            Step(*scenes, editor, settings);
        }
        if (state_ == State::Closed) {
            return;
        }
        if (requestOpen_) {
            ImGui::OpenPopup(kTitle);
            requestOpen_ = false;
        }

        const float fontSize = ImGui::GetFontSize();
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(fontSize * 36.0f, 0.0f), ImVec2(fontSize * 36.0f, FLT_MAX));

        // 保存し直している間は閉じさせない
        bool open = true;
        const bool visible = ImGui::BeginPopupModal(kTitle, state_ == State::Running ? nullptr : &open,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
        if (!visible) {
            if (!ImGui::IsPopupOpen(kTitle) && state_ != State::Running) {
                state_ = State::Closed;
            }
            return;
        }

        switch (state_) {
        case State::Ready:   DrawReady(scenes, editor); break;
        case State::Running: DrawRunning(); break;
        case State::Done:    DrawDone(); break;
        default: break;
        }
        if (state_ == State::Closed) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void SceneResaveDialog::DrawReady(SceneManager* scenes, SceneDebugEditor* editor)
    {
        const bool editing = PlaybackStateManager::GetInstance().IsEditing();
        const bool dirty = editor && editor->IsSceneDirty();

        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(std::format(
            "保存データのあるシーン {} 個を順に開いて、今の書き方で保存し直します。"
            "プロジェクトの設定のファイルも書き直します。終わったら今のシーンへ戻ります。",
            SceneSaveSystem::ListSavedScenes().size()).c_str());
        ImGui::TextColored(Theme::kTextMute, "%s",
            "エンジンの保存の書き方が変わったときに、差分を 1 回にまとめて出すために使います。");
        if (!editing) {
            ImGui::TextColored(Theme::kWarn, "%s", "再生中は使えません。停止してください。");
        } else if (dirty) {
            ImGui::TextColored(Theme::kWarn, "%s", "保存していない変更があります。先にシーンを保存してください。");
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const char* cancelLabel = "やめる";
        const char* startLabel = "保存し直す";
        AlignButtons(ButtonWidth(cancelLabel) + ButtonWidth(startLabel) + ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button(cancelLabel)) {
            state_ = State::Closed;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!scenes || !editing || dirty);
        if (ImGui::Button(startLabel)) {
            Start(*scenes);
        }
        ImGui::EndDisabled();
    }

    void SceneResaveDialog::DrawRunning()
    {
        const std::size_t total = scenes_.size();
        const std::size_t done = (std::min)(next_, total);
        const std::string label = waitingFor_.empty()
            ? std::string("設定のファイルを書き直しています")
            : std::format("{} を開いています（{} / {}）", waitingFor_, done, total);
        ImGui::TextUnformatted(label.c_str());
        ImGui::ProgressBar(total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 1.0f,
            ImVec2(-FLT_MIN, 0.0f));
    }

    void SceneResaveDialog::DrawDone()
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(std::format("シーン {} 個と設定のファイル {} 個を保存し直しました。",
            savedCount_, settingsCount_).c_str());
        if (!failed_.empty()) {
            std::string names;
            for (const std::string& name : failed_) {
                names += names.empty() ? name : "、" + name;
            }
            ImGui::TextColored(Theme::kError, "開けなかったシーン: %s", names.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const char* closeLabel = "閉じる";
        AlignButtons(ButtonWidth(closeLabel));
        if (ImGui::Button(closeLabel)) {
            state_ = State::Closed;
        }
    }

    void SceneResaveDialog::AlignButtons(float width)
    {
        const float x = ImGui::GetContentRegionMax().x - width;
        if (x > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(x);
        }
    }
}

#endif // CORE_EDITOR
