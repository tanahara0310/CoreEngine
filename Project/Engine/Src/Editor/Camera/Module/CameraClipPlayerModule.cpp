#include "pch.h"
#include "CameraClipPlayerModule.h"
#include "Camera/Sequence/CameraSequenceEvaluator.h"
#include "Camera/Sequence/CameraSequenceIO.h"

#ifdef USE_IMGUI

#include "Editor/ImGui/ImGuiAll.h"
#include <algorithm>
#include <filesystem>

#include "Camera/CameraManager.h"
#include "Camera/Camera.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"

namespace CoreEngine
{
    namespace
    {
        /// @brief 名前でシーンのオブジェクトを引く注視解決口を作る
        CameraSequenceAimContext MakeAimContext(GameObjectManager* objects)
        {
            CameraSequenceAimContext context{};
            if (!objects) {
                return context;
            }

            context.resolveObject = [objects](const std::string& name, Vector3& outPosition) {
                for (const auto& object : objects->GetAllObjects()) {
                    if (object && object->GetName() == name) {
                        outPosition = object->GetWorldPosition();
                        return true;
                    }
                }
                return false;
            };
            return context;
        }
    }

    // 再生中だけ playhead を進め、補間したカメラ姿勢をゲームカメラへ書き込む
    void CameraClipPlayerModule::Update(const CameraEditorContext& context)
    {
        if (!context.cameraManager || !isPlaying_ || clip_.keyframes.empty()) {
            return;
        }

        // 再生ヘッドを進め、シーケンスの補間結果を現在カメラに適用する。
        playhead_ += ImGui::GetIO().DeltaTime * playbackSpeed_;

        if (playhead_ > clip_.timelineLength) {
            if (loopPlayback_) {
                playhead_ = 0.0f;
            } else {
                playhead_ = clip_.timelineLength;
                isPlaying_ = false;
            }
        }

        const CameraSequenceAimContext aimContext = MakeAimContext(context.gameObjectManager);

        CameraSnapshot evaluated{};
        if (CameraSequenceEvaluator::Evaluate(clip_, playhead_, evaluated, &aimContext)) {
            ApplyToActiveCamera(context, evaluated);
        }
    }

    void CameraClipPlayerModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) {
            return;
        }

        // ファイル一覧はディスク走査なので、要求があったフレームだけ取り直す
        if (needRefreshClipFileList_) {
            RefreshClipFileList();
        }

        ImGui::Text("アクティブ3D: %s", context.cameraManager->GetActiveCameraName(CameraType::Camera3D).c_str());
        ImGui::Text("読み込み中シーケンス: %s", loadedClipName_.empty() ? "なし" : loadedClipName_.c_str());
        UI::Hint("ここで読み込んだシーケンスがゲームカメラ再生データです。");
        UI::Separator();

        if (ImGui::Button("シーケンス一覧を更新")) {
            needRefreshClipFileList_ = true;
        }

        if (clipFileList_.empty()) {
            UI::Hint("保存済みシーケンスがありません。");
        } else {
            selectedClipFileIndex_ = std::clamp(selectedClipFileIndex_, -1, static_cast<int>(clipFileList_.size()) - 1);

            if (auto lb = UI::Scope::ListBoxScope("保存済みシーケンス", ImVec2(-1.0f, 140.0f))) {
                for (int i = 0; i < static_cast<int>(clipFileList_.size()); ++i) {
                    const bool isSelected = (selectedClipFileIndex_ == i);
                    if (ImGui::Selectable(clipFileList_[i].c_str(), isSelected)) {
                        selectedClipFileIndex_ = i;
                    }
                }
            }

            if (selectedClipFileIndex_ >= 0 && selectedClipFileIndex_ < static_cast<int>(clipFileList_.size())) {
                if (ImGui::Button("選択シーケンスを読み込み")) {
                    const std::filesystem::path fullPath = std::filesystem::path(clipDirectoryPath_) / clipFileList_[selectedClipFileIndex_];
                    if (!LoadClipFromFile(fullPath.string())) {
                        statusMessage_ = "シーケンス読み込みに失敗しました。";
                    }
                }
            }
        }

        UI::Separator();

        if (!statusMessage_.empty()) {
            UI::Hint(statusMessage_.c_str());
        }

        if (clip_.keyframes.empty()) {
            UI::Hint("再生可能なシーケンスが読み込まれていません。");
            return;
        }

        ImGui::Text("キーフレーム数: %d", static_cast<int>(clip_.keyframes.size()));
        ImGui::Text("ショット数: %d (%s)", static_cast<int>(clip_.shots.size()), clip_.shotsEnabled ? "有効" : "無効");
        UI::DragFloat("再生速度", playbackSpeed_, 0.05f, 0.1f, 4.0f, "%.2fx");
        UI::Widgets::ToggleSwitch("ループ再生", &loopPlayback_);

        ImGui::Text("補間タイプ: %s", CameraSequenceEasing::LabelAt(clip_.easingTypeIndex));

        bool playheadChanged = UI::SliderFloat("再生ヘッド", playhead_, 0.0f, clip_.timelineLength, "%.2f 秒");

        if (isPlaying_) {
            if (ImGui::Button("停止")) {
                isPlaying_ = false;
            }
        } else {
            if (ImGui::Button("再生")) {
                isPlaying_ = true;
            }
        }

        UI::SameLine();
        if (ImGui::Button("先頭へ")) {
            playhead_ = 0.0f;
            playheadChanged = true;
        }

        if (!isPlaying_ && playheadChanged) {
            const CameraSequenceAimContext aimContext = MakeAimContext(context.gameObjectManager);

            CameraSnapshot evaluated{};
            if (CameraSequenceEvaluator::Evaluate(clip_, playhead_, evaluated, &aimContext)) {
                ApplyToActiveCamera(context, evaluated);
            }
        }
    }

    void CameraClipPlayerModule::RefreshClipFileList()
    {
        clipFileList_ = CameraSequenceIO::GetSequenceFileList(clipDirectoryPath_);
        needRefreshClipFileList_ = false;
    }

    bool CameraClipPlayerModule::LoadClipFromFile(const std::string& filePath)
    {
        // 読み込み成功時だけ内部状態を差し替える。失敗しても再生中のクリップは壊さない
        CameraSequenceAsset asset{};
        if (!CameraSequenceIO::Load(filePath, asset)) {
            return false;
        }

        // タイムライン長の下限・時刻の並びは CameraSequenceIO::Load が保証する。
        clip_ = std::move(asset);

        playhead_ = 0.0f;
        isPlaying_ = false;
        loadedClipName_ = std::filesystem::path(filePath).filename().string();
        statusMessage_.clear();
        return !clip_.keyframes.empty();
    }

    bool CameraClipPlayerModule::ApplyToActiveCamera(const CameraEditorContext& context, const CameraSnapshot& snapshot) const
    {
        Camera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (!active3D) {
            return false;
        }

        active3D->RestoreSnapshot(snapshot);
        return true;
    }
}

#endif // USE_IMGUI
