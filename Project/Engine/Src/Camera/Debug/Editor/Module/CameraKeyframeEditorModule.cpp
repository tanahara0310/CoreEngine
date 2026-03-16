#include "CameraKeyframeEditorModule.h"

#ifdef _DEBUG

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "Camera/CameraManager.h"
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Release/Camera.h"
#include "Graphics/Line/LineManager.h"

namespace CoreEngine
{
    namespace
    {
        struct EasingOption {
            const char* label;
            EasingUtil::Type type;
        };

        constexpr EasingOption kEasingOptions[] = {
            { "線形", EasingUtil::Type::Linear },
            { "イーズイン (Quad)", EasingUtil::Type::EaseInQuad },
            { "イーズアウト (Quad)", EasingUtil::Type::EaseOutQuad },
            { "イーズインアウト (Quad)", EasingUtil::Type::EaseInOutQuad },
            { "イーズイン (Cubic)", EasingUtil::Type::EaseInCubic },
            { "イーズアウト (Cubic)", EasingUtil::Type::EaseOutCubic },
            { "イーズインアウト (Cubic)", EasingUtil::Type::EaseInOutCubic },
            { "イーズイン (Quart)", EasingUtil::Type::EaseInQuart },
            { "イーズアウト (Quart)", EasingUtil::Type::EaseOutQuart },
            { "イーズインアウト (Quart)", EasingUtil::Type::EaseInOutQuart },
            { "イーズイン (Sine)", EasingUtil::Type::EaseInSine },
            { "イーズアウト (Sine)", EasingUtil::Type::EaseOutSine },
            { "イーズインアウト (Sine)", EasingUtil::Type::EaseInOutSine },
            { "イーズイン (Expo)", EasingUtil::Type::EaseInExpo },
            { "イーズアウト (Expo)", EasingUtil::Type::EaseOutExpo },
            { "イーズインアウト (Expo)", EasingUtil::Type::EaseInOutExpo }
        };

        constexpr int kEasingOptionCount = static_cast<int>(sizeof(kEasingOptions) / sizeof(kEasingOptions[0]));
    }

    void CameraKeyframeEditorModule::Update(const CameraEditorContext& context)
    {
        if (!context.cameraManager || !isPlaying_ || keyframes_.size() < 2) {
            UpdateAutoKey(context);
            DrawViewportVisualization();
            return;
        }

        // 再生ヘッドを進め、補間結果を現在カメラへ適用する。
        playhead_ += ImGui::GetIO().DeltaTime * playbackSpeed_;

        if (playhead_ > timelineLength_) {
            if (loopPlayback_) {
                playhead_ = 0.0f;
            } else {
                playhead_ = timelineLength_;
                isPlaying_ = false;
            }
        }

        CameraSnapshot evaluated{};
        if (EvaluateSnapshotAt(playhead_, evaluated)) {
            ApplyToActiveCamera(context, evaluated);
        }

        UpdateAutoKey(context);
        DrawViewportVisualization();
    }

    void CameraKeyframeEditorModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            Undo();
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            Redo();
        }

        if (needRefreshClipFileList_) {
            RefreshClipFileList();
        }

        ImGui::Text("アクティブ3D: %s", context.cameraManager->GetActiveCameraName(CameraType::Camera3D).c_str());
        if (ImGui::Button("Undo")) {
            Undo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Redo")) {
            Redo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("ショートカット: Ctrl+Z / Ctrl+Y");
        ImGui::Separator();

        ImGui::Checkbox("Auto Key", &autoKeyEnabled_);
        ImGui::SameLine();
        ImGui::TextDisabled("Transform/Parametersの変更時に自動でキーを作成・更新");

        ImGui::SeparatorText("ビューポート可視化");
        ImGui::Checkbox("可視化を有効", &viewportVisualizationEnabled_);
        ImGui::Checkbox("カメラ軌跡", &viewportShowTrajectory_);
        ImGui::Checkbox("キーフレーム位置", &viewportShowKeyMarkers_);
        ImGui::Checkbox("DebugCamera注視点", &viewportShowDebugTarget_);
        ImGui::DragInt("軌跡サンプル/区間", &viewportTrajectorySamplesPerSegment_, 1.0f, 2, 64);
        ImGui::DragFloat("マーカーサイズ", &viewportMarkerSize_, 0.01f, 0.02f, 2.0f, "%.2f");
        ImGui::SliderFloat("軌跡アルファ", &viewportTrajectoryAlpha_, 0.1f, 1.0f, "%.2f");
        ImGui::ColorEdit3("軌跡色", &viewportTrajectoryColor_.x);
        ImGui::ColorEdit3("キー色", &viewportKeyMarkerColor_.x);
        ImGui::ColorEdit3("選択キー色", &viewportSelectedKeyColor_.x);
        ImGui::ColorEdit3("注視点色", &viewportDebugTargetColor_.x);

        viewportTrajectorySamplesPerSegment_ = std::clamp(viewportTrajectorySamplesPerSegment_, 2, 64);
        viewportMarkerSize_ = std::clamp(viewportMarkerSize_, 0.02f, 2.0f);

        // タイムライン長と再生ヘッドを編集する。
        ImGui::DragFloat("タイムライン長(秒)", &timelineLength_, 0.1f, 0.1f, 600.0f, "%.2f");
        if (timelineLength_ < 0.1f) {
            timelineLength_ = 0.1f;
        }

        bool playheadChanged = ImGui::SliderFloat("再生ヘッド", &playhead_, 0.0f, timelineLength_, "%.2f 秒");

        // タイムライン上のキーフレームを直接クリックして移動できるように可視化する。
        {
            const ImVec2 region = ImGui::GetContentRegionAvail();
            const float timelineHeight = 34.0f;
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const ImVec2 size((std::max)(region.x, 120.0f), timelineHeight);

            ImGui::InvisibleButton("##KeyframeTimeline", size);
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            const ImU32 lineColor = IM_COL32(120, 120, 120, 255);
            const ImU32 keyColor = IM_COL32(80, 170, 255, 255);
            const ImU32 selectedKeyColor = IM_COL32(255, 205, 80, 255);
            const ImU32 playheadColor = IM_COL32(255, 120, 120, 255);

            const float centerY = cursor.y + size.y * 0.5f;
            drawList->AddLine(ImVec2(cursor.x, centerY), ImVec2(cursor.x + size.x, centerY), lineColor, 2.0f);

            // キーフレームマーカー描画
            for (int i = 0; i < static_cast<int>(keyframes_.size()); ++i) {
                const float normalized = (timelineLength_ > 0.0f) ? (keyframes_[i].time / timelineLength_) : 0.0f;
                const float x = cursor.x + (std::clamp(normalized, 0.0f, 1.0f) * size.x);
                const ImU32 color = (i == selectedIndex_) ? selectedKeyColor : keyColor;
                drawList->AddCircleFilled(ImVec2(x, centerY), 4.5f, color);
            }

            // 再生ヘッド描画
            const float playheadX = cursor.x + ((playhead_ / timelineLength_) * size.x);
            drawList->AddLine(ImVec2(playheadX, cursor.y), ImVec2(playheadX, cursor.y + size.y), playheadColor, 2.0f);

            // クリック時、近いキーフレームがあれば選択、なければ再生ヘッド移動
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const float mouseX = ImGui::GetMousePos().x;
                int nearest = -1;
                float nearestPixelDist = 10.0f;

                for (int i = 0; i < static_cast<int>(keyframes_.size()); ++i) {
                    const float normalized = (timelineLength_ > 0.0f) ? (keyframes_[i].time / timelineLength_) : 0.0f;
                    const float x = cursor.x + (std::clamp(normalized, 0.0f, 1.0f) * size.x);
                    const float d = std::fabs(mouseX - x);
                    if (d < nearestPixelDist) {
                        nearestPixelDist = d;
                        nearest = i;
                    }
                }

                if (nearest >= 0) {
                    selectedIndex_ = nearest;
                    playhead_ = keyframes_[nearest].time;
                    playheadChanged = true;
                } else {
                    const float normalizedMouse = std::clamp((mouseX - cursor.x) / size.x, 0.0f, 1.0f);
                    playhead_ = normalizedMouse * timelineLength_;
                    playheadChanged = true;
                }
            }
        }

        if (isPlaying_) {
            if (ImGui::Button("停止")) {
                isPlaying_ = false;
            }
        } else {
            if (ImGui::Button("再生")) {
                isPlaying_ = true;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("先頭へ")) {
            playhead_ = 0.0f;
            if (!keyframes_.empty()) {
                CameraSnapshot evaluated{};
                if (EvaluateSnapshotAt(playhead_, evaluated)) {
                    ApplyToActiveCamera(context, evaluated);
                }
            }
        }

        ImGui::SameLine();
        ImGui::Checkbox("ループ再生", &loopPlayback_);

        ImGui::DragFloat("再生速度", &playbackSpeed_, 0.05f, 0.1f, 4.0f, "%.2fx");

        if (easingTypeIndex_ < 0 || easingTypeIndex_ >= kEasingOptionCount) {
            easingTypeIndex_ = 0;
        }

        if (ImGui::BeginCombo("補間タイプ", kEasingOptions[easingTypeIndex_].label)) {
            for (int i = 0; i < kEasingOptionCount; ++i) {
                const bool selected = (i == easingTypeIndex_);
                if (ImGui::Selectable(kEasingOptions[i].label, selected)) {
                    easingTypeIndex_ = i;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // 前後キーフレームへワンクリック移動できるようにし、編集導線を短くする。
        if (ImGui::Button("前のキーへ")) {
            const int prev = FindPreviousKeyframeIndex(playhead_ - updateThreshold_);
            if (prev >= 0) {
                selectedIndex_ = prev;
                playhead_ = keyframes_[prev].time;
                playheadChanged = true;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("次のキーへ")) {
            const int next = FindNextKeyframeIndex(playhead_ + updateThreshold_);
            if (next >= 0) {
                selectedIndex_ = next;
                playhead_ = keyframes_[next].time;
                playheadChanged = true;
            }
        }

        // 現在時刻の近傍キーフレームがあれば更新、なければ追加する。
        if (ImGui::Button("現在位置にキーフレームを追加/更新")) {
            CameraSnapshot snapshot;
            if (CaptureFromActiveCamera(context, snapshot)) {
                PushUndoState();
                const int nearest = FindNearestKeyframeIndex(playhead_);
                if (nearest >= 0 && std::fabs(keyframes_[nearest].time - playhead_) <= updateThreshold_) {
                    keyframes_[nearest].snapshot = snapshot;
                    selectedIndex_ = nearest;
                } else {
                    Keyframe key{};
                    key.time = playhead_;
                    key.snapshot = snapshot;
                    keyframes_.push_back(key);
                    std::sort(keyframes_.begin(), keyframes_.end(),
                        [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });

                    selectedIndex_ = FindNearestKeyframeIndex(playhead_);
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("選択キーフレームを複製")) {
            if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(keyframes_.size())) {
                PushUndoState();

                Keyframe duplicated = keyframes_[selectedIndex_];
                duplicated.time = std::clamp(duplicated.time + 0.1f, 0.0f, timelineLength_);
                if (std::fabs(duplicated.time - keyframes_[selectedIndex_].time) <= updateThreshold_) {
                    duplicated.time = std::clamp(duplicated.time + updateThreshold_, 0.0f, timelineLength_);
                }

                keyframes_.push_back(duplicated);
                std::sort(keyframes_.begin(), keyframes_.end(),
                    [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });

                selectedIndex_ = FindNearestKeyframeIndex(duplicated.time);
                playhead_ = duplicated.time;
                playheadChanged = true;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("選択キーフレームを削除")) {
            if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(keyframes_.size())) {
                PushUndoState();
                keyframes_.erase(keyframes_.begin() + selectedIndex_);
                if (keyframes_.empty()) {
                    selectedIndex_ = -1;
                } else if (selectedIndex_ >= static_cast<int>(keyframes_.size())) {
                    selectedIndex_ = static_cast<int>(keyframes_.size()) - 1;
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("全キーフレームをクリア")) {
            PushUndoState();
            keyframes_.clear();
            selectedIndex_ = -1;
        }

        ImGui::Separator();

        if (keyframes_.empty()) {
            ImGui::TextDisabled("キーフレームがありません。");
            return;
        }

        // キーフレーム一覧表示
        if (ImGui::BeginListBox("キーフレーム一覧", ImVec2(-1.0f, 180.0f))) {
            for (int i = 0; i < static_cast<int>(keyframes_.size()); ++i) {
                const bool isSelected = (i == selectedIndex_);
                const char* cameraTypeLabel = keyframes_[i].snapshot.isDebugCamera ? "デバッグ" : "リリース";
                char timeLabel[32]{};
                std::snprintf(timeLabel, sizeof(timeLabel), "%.2f秒", keyframes_[i].time);
                const std::string label = std::string(timeLabel) + " [" + cameraTypeLabel + "]";
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selectedIndex_ = i;
                }
            }
            ImGui::EndListBox();
        }

        // 選択したキーフレームを現在のアクティブカメラへ適用
        if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(keyframes_.size())) {
            if (ImGui::Button("選択キーフレームを適用")) {
                ApplyToActiveCamera(context, keyframes_[selectedIndex_].snapshot);
            }

            ImGui::SameLine();
            if (ImGui::Button("再生ヘッドを選択位置へ移動")) {
                playhead_ = keyframes_[selectedIndex_].time;
                playheadChanged = true;
            }

            // 選択キーの時刻を直接編集できるようにする。
            float selectedTime = keyframes_[selectedIndex_].time;
            if (ImGui::DragFloat("選択キー時刻(秒)", &selectedTime, 0.05f, 0.0f, timelineLength_, "%.2f")) {
                PushUndoState();
                keyframes_[selectedIndex_].time = std::clamp(selectedTime, 0.0f, timelineLength_);
                std::sort(keyframes_.begin(), keyframes_.end(),
                    [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
                selectedIndex_ = FindNearestKeyframeIndex(selectedTime);
                playhead_ = std::clamp(selectedTime, 0.0f, timelineLength_);
                playheadChanged = true;
            }
        }

        // 再生停止中に再生ヘッドを動かした場合は、その時刻の値を即時反映する。
        if (!isPlaying_ && playheadChanged) {
            CameraSnapshot evaluated{};
            if (EvaluateSnapshotAt(playhead_, evaluated)) {
                ApplyToActiveCamera(context, evaluated);
            }
        }

        ImGui::Separator();
        ImGui::Text("クリップ保存/読み込み");
        ImGui::InputText("クリップ名", clipFileNameBuffer_, sizeof(clipFileNameBuffer_));

        if (ImGui::Button("クリップを保存")) {
            std::string fileName = clipFileNameBuffer_;
            if (!fileName.empty()) {
                if (fileName.find(".json") == std::string::npos) {
                    fileName += ".json";
                }

                JsonManager::GetInstance().CreateJsonDirectory(clipDirectoryPath_);
                const std::filesystem::path fullPath = std::filesystem::path(clipDirectoryPath_) / fileName;
                if (SaveCurrentClipToFile(fullPath.string())) {
                    needRefreshClipFileList_ = true;
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("一覧更新")) {
            needRefreshClipFileList_ = true;
        }

        if (clipFileList_.empty()) {
            ImGui::TextDisabled("保存済みクリップがありません。");
        } else {
            selectedClipFileIndex_ = std::clamp(selectedClipFileIndex_, -1, static_cast<int>(clipFileList_.size()) - 1);

            if (ImGui::BeginListBox("クリップ一覧", ImVec2(-1.0f, 120.0f))) {
                for (int i = 0; i < static_cast<int>(clipFileList_.size()); ++i) {
                    const bool isSelected = (selectedClipFileIndex_ == i);
                    if (ImGui::Selectable(clipFileList_[i].c_str(), isSelected)) {
                        selectedClipFileIndex_ = i;
                    }
                }
                ImGui::EndListBox();
            }

            if (selectedClipFileIndex_ >= 0 && selectedClipFileIndex_ < static_cast<int>(clipFileList_.size())) {
                if (ImGui::Button("選択クリップを読み込み")) {
                    const std::filesystem::path fullPath = std::filesystem::path(clipDirectoryPath_) / clipFileList_[selectedClipFileIndex_];
                    PushUndoState();
                    if (LoadClipFromFile(fullPath.string())) {
                        // 先頭状態を反映し、読み込み後すぐ内容を確認できるようにする。
                        CameraSnapshot evaluated{};
                        if (EvaluateSnapshotAt(playhead_, evaluated)) {
                            ApplyToActiveCamera(context, evaluated);
                        }
                    } else {
                        // 読み込み失敗時は不要なUndo履歴を取り消す。
                        if (!undoStack_.empty()) {
                            undoStack_.pop_back();
                        }
                    }
                }
            }
        }
    }

    bool CameraKeyframeEditorModule::EvaluateSnapshotAt(float time, CameraSnapshot& outSnapshot) const
    {
        if (keyframes_.empty()) {
            return false;
        }

        if (keyframes_.size() == 1) {
            outSnapshot = keyframes_.front().snapshot;
            return true;
        }

        const float clampedTime = std::clamp(time, 0.0f, timelineLength_);

        // 範囲外は端のキーフレームを返す。
        if (clampedTime <= keyframes_.front().time) {
            outSnapshot = keyframes_.front().snapshot;
            return true;
        }
        if (clampedTime >= keyframes_.back().time) {
            outSnapshot = keyframes_.back().snapshot;
            return true;
        }

        // 区間を探索して線形補間する。
        for (size_t i = 0; i + 1 < keyframes_.size(); ++i) {
            const Keyframe& from = keyframes_[i];
            const Keyframe& to = keyframes_[i + 1];

            if (clampedTime >= from.time && clampedTime <= to.time) {
                const float span = to.time - from.time;
                if (span <= 0.0001f) {
                    outSnapshot = from.snapshot;
                    return true;
                }

                const float t = (clampedTime - from.time) / span;
                outSnapshot = InterpolateSnapshot(from.snapshot, to.snapshot, t);
                return true;
            }
        }

        outSnapshot = keyframes_.back().snapshot;
        return true;
    }

    CameraSnapshot CameraKeyframeEditorModule::InterpolateSnapshot(const CameraSnapshot& from, const CameraSnapshot& to, float t) const
    {
        // カメラタイプが異なる場合は始点スナップショットを優先する。
        if (from.isDebugCamera != to.isDebugCamera) {
            return from;
        }

        CameraSnapshot result{};
        result.isDebugCamera = from.isDebugCamera;
        const EasingUtil::Type easingType = GetSelectedEasingType();

        if (result.isDebugCamera) {
            result.target = EasingUtil::LerpVector3(from.target, to.target, t, easingType);
            result.distance = EasingUtil::Lerp(from.distance, to.distance, t, easingType);
            result.pitch = EasingUtil::LerpAngle(from.pitch, to.pitch, t, easingType);
            result.yaw = EasingUtil::LerpAngle(from.yaw, to.yaw, t, easingType);
        } else {
            result.position = EasingUtil::LerpVector3(from.position, to.position, t, easingType);
            result.rotation = Vector3(
                EasingUtil::LerpAngle(from.rotation.x, to.rotation.x, t, easingType),
                EasingUtil::LerpAngle(from.rotation.y, to.rotation.y, t, easingType),
                EasingUtil::LerpAngle(from.rotation.z, to.rotation.z, t, easingType));
            result.scale = EasingUtil::LerpVector3(from.scale, to.scale, t, easingType);
        }

        result.parameters.fov = EasingUtil::Lerp(from.parameters.fov, to.parameters.fov, t, easingType);
        result.parameters.nearClip = EasingUtil::Lerp(from.parameters.nearClip, to.parameters.nearClip, t, easingType);
        result.parameters.farClip = EasingUtil::Lerp(from.parameters.farClip, to.parameters.farClip, t, easingType);
        result.parameters.aspectRatio = EasingUtil::Lerp(from.parameters.aspectRatio, to.parameters.aspectRatio, t, easingType);
        return result;
    }

    EasingUtil::Type CameraKeyframeEditorModule::GetSelectedEasingType() const
    {
        if (easingTypeIndex_ < 0 || easingTypeIndex_ >= kEasingOptionCount) {
            return EasingUtil::Type::Linear;
        }

        return kEasingOptions[easingTypeIndex_].type;
    }

    bool CameraKeyframeEditorModule::CaptureFromActiveCamera(const CameraEditorContext& context, CameraSnapshot& outSnapshot) const
    {
        ICamera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (!active3D) {
            return false;
        }

        if (auto* releaseCamera = dynamic_cast<Camera*>(active3D)) {
            outSnapshot = releaseCamera->CaptureSnapshot("Keyframe");
            return true;
        }

        if (auto* debugCamera = dynamic_cast<DebugCamera*>(active3D)) {
            outSnapshot = debugCamera->CaptureSnapshot("Keyframe");
            return true;
        }

        return false;
    }

    bool CameraKeyframeEditorModule::ApplyToActiveCamera(const CameraEditorContext& context, const CameraSnapshot& snapshot)
    {
        ICamera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (!active3D) {
            return false;
        }

        if (snapshot.isDebugCamera) {
            if (auto* debugCamera = dynamic_cast<DebugCamera*>(active3D)) {
                debugCamera->RestoreSnapshot(snapshot);
                ignoreNextAutoKey_ = true;
                observedSnapshot_ = snapshot;
                hasObservedSnapshot_ = true;
                return true;
            }
            return false;
        }

        if (auto* releaseCamera = dynamic_cast<Camera*>(active3D)) {
            releaseCamera->RestoreSnapshot(snapshot);
            ignoreNextAutoKey_ = true;
            observedSnapshot_ = snapshot;
            hasObservedSnapshot_ = true;
            return true;
        }

        return false;
    }

    bool CameraKeyframeEditorModule::IsSameSnapshot(const CameraSnapshot& lhs, const CameraSnapshot& rhs) const
    {
        constexpr float epsilon = 0.0001f;

        const auto nearEqual = [](float a, float b) {
            return std::fabs(a - b) <= 0.0001f;
        };

        if (lhs.isDebugCamera != rhs.isDebugCamera) {
            return false;
        }

        if (!nearEqual(lhs.parameters.fov, rhs.parameters.fov)
            || !nearEqual(lhs.parameters.nearClip, rhs.parameters.nearClip)
            || !nearEqual(lhs.parameters.farClip, rhs.parameters.farClip)
            || !nearEqual(lhs.parameters.aspectRatio, rhs.parameters.aspectRatio)) {
            return false;
        }

        if (lhs.isDebugCamera) {
            return nearEqual(lhs.target.x, rhs.target.x)
                && nearEqual(lhs.target.y, rhs.target.y)
                && nearEqual(lhs.target.z, rhs.target.z)
                && nearEqual(lhs.distance, rhs.distance)
                && nearEqual(lhs.pitch, rhs.pitch)
                && nearEqual(lhs.yaw, rhs.yaw);
        }

        return nearEqual(lhs.position.x, rhs.position.x)
            && nearEqual(lhs.position.y, rhs.position.y)
            && nearEqual(lhs.position.z, rhs.position.z)
            && nearEqual(lhs.rotation.x, rhs.rotation.x)
            && nearEqual(lhs.rotation.y, rhs.rotation.y)
            && nearEqual(lhs.rotation.z, rhs.rotation.z)
            && nearEqual(lhs.scale.x, rhs.scale.x)
            && nearEqual(lhs.scale.y, rhs.scale.y)
            && nearEqual(lhs.scale.z, rhs.scale.z)
            && std::fabs(lhs.parameters.fov - rhs.parameters.fov) <= epsilon;
    }

    void CameraKeyframeEditorModule::UpdateAutoKey(const CameraEditorContext& context)
    {
        if (!context.cameraManager || !autoKeyEnabled_ || isPlaying_) {
            autoKeyEditing_ = false;
            return;
        }

        CameraSnapshot current{};
        if (!CaptureFromActiveCamera(context, current)) {
            autoKeyEditing_ = false;
            hasObservedSnapshot_ = false;
            return;
        }

        if (ignoreNextAutoKey_) {
            ignoreNextAutoKey_ = false;
            observedSnapshot_ = current;
            hasObservedSnapshot_ = true;
            autoKeyEditing_ = false;
            return;
        }

        if (!hasObservedSnapshot_) {
            observedSnapshot_ = current;
            hasObservedSnapshot_ = true;
            autoKeyEditing_ = false;
            return;
        }

        const bool changed = !IsSameSnapshot(observedSnapshot_, current);
        if (!changed) {
            autoKeyEditing_ = false;
            return;
        }

        if (!autoKeyEditing_) {
            PushUndoState();
            autoKeyEditing_ = true;
        }

        const int nearest = FindNearestKeyframeIndex(playhead_);
        if (nearest >= 0 && std::fabs(keyframes_[nearest].time - playhead_) <= updateThreshold_) {
            keyframes_[nearest].snapshot = current;
            selectedIndex_ = nearest;
        } else {
            Keyframe key{};
            key.time = playhead_;
            key.snapshot = current;
            keyframes_.push_back(key);
            std::sort(keyframes_.begin(), keyframes_.end(),
                [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });

            selectedIndex_ = FindNearestKeyframeIndex(playhead_);
        }

        observedSnapshot_ = current;
    }

    void CameraKeyframeEditorModule::DrawViewportVisualization()
    {
        if (!viewportVisualizationEnabled_ || keyframes_.empty()) {
            return;
        }

        auto& lineManager = LineManager::GetInstance();

        if (viewportShowTrajectory_ && keyframes_.size() >= 2) {
            // 区間ごとの補間結果を細かくサンプルし、Sceneビュー上で軌跡として可視化する。
            for (size_t i = 0; i + 1 < keyframes_.size(); ++i) {
                const Keyframe& from = keyframes_[i];
                const Keyframe& to = keyframes_[i + 1];

                if (to.time <= from.time) {
                    continue;
                }

                Vector3 prev = GetSnapshotWorldPosition(from.snapshot);
                for (int sampleIndex = 1; sampleIndex <= viewportTrajectorySamplesPerSegment_; ++sampleIndex) {
                    const float localT = static_cast<float>(sampleIndex) / static_cast<float>(viewportTrajectorySamplesPerSegment_);
                    CameraSnapshot interpolated = InterpolateSnapshot(from.snapshot, to.snapshot, localT);
                    const Vector3 current = GetSnapshotWorldPosition(interpolated);
                    lineManager.DrawLine(prev, current, viewportTrajectoryColor_, viewportTrajectoryAlpha_);
                    prev = current;
                }
            }
        }

        if (viewportShowKeyMarkers_) {
            // キーフレーム位置をマーカー表示し、選択中キーを色で区別する。
            for (int i = 0; i < static_cast<int>(keyframes_.size()); ++i) {
                const Vector3 position = GetSnapshotWorldPosition(keyframes_[i].snapshot);
                const Vector3 color = (i == selectedIndex_) ? viewportSelectedKeyColor_ : viewportKeyMarkerColor_;
                lineManager.DrawWireSphere(position, viewportMarkerSize_, 8, color, 0.95f);

                if (viewportShowDebugTarget_ && keyframes_[i].snapshot.isDebugCamera) {
                    lineManager.DrawCross(keyframes_[i].snapshot.target, viewportMarkerSize_ * 1.2f, viewportDebugTargetColor_, 0.95f);
                    lineManager.DrawLine(position, keyframes_[i].snapshot.target, viewportDebugTargetColor_, 0.5f);
                }
            }
        }
    }

    Vector3 CameraKeyframeEditorModule::GetSnapshotWorldPosition(const CameraSnapshot& snapshot) const
    {
        if (!snapshot.isDebugCamera) {
            return snapshot.position;
        }

        return {
            snapshot.target.x + snapshot.distance * std::cos(snapshot.pitch) * std::sin(snapshot.yaw),
            snapshot.target.y + snapshot.distance * std::sin(snapshot.pitch),
            snapshot.target.z + snapshot.distance * std::cos(snapshot.pitch) * std::cos(snapshot.yaw)
        };
    }

    int CameraKeyframeEditorModule::FindNearestKeyframeIndex(float time) const
    {
        if (keyframes_.empty()) {
            return -1;
        }

        int bestIndex = 0;
        float bestDistance = std::fabs(keyframes_[0].time - time);

        for (int i = 1; i < static_cast<int>(keyframes_.size()); ++i) {
            const float distance = std::fabs(keyframes_[i].time - time);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = i;
            }
        }

        return bestIndex;
    }

    int CameraKeyframeEditorModule::FindPreviousKeyframeIndex(float time) const
    {
        int index = -1;
        for (int i = 0; i < static_cast<int>(keyframes_.size()); ++i) {
            if (keyframes_[i].time <= time) {
                index = i;
            } else {
                break;
            }
        }
        return index;
    }

    int CameraKeyframeEditorModule::FindNextKeyframeIndex(float time) const
    {
        for (int i = 0; i < static_cast<int>(keyframes_.size()); ++i) {
            if (keyframes_[i].time >= time) {
                return i;
            }
        }
        return -1;
    }

    json CameraKeyframeEditorModule::SnapshotToJson(const CameraSnapshot& snapshot) const
    {
        json jsonData;

        jsonData["isDebugCamera"] = snapshot.isDebugCamera;
        if (snapshot.isDebugCamera) {
            jsonData["target"] = JsonManager::Vector3ToJson(snapshot.target);
            jsonData["distance"] = snapshot.distance;
            jsonData["pitch"] = snapshot.pitch;
            jsonData["yaw"] = snapshot.yaw;
        } else {
            jsonData["position"] = JsonManager::Vector3ToJson(snapshot.position);
            jsonData["rotation"] = JsonManager::Vector3ToJson(snapshot.rotation);
            jsonData["scale"] = JsonManager::Vector3ToJson(snapshot.scale);
        }

        json paramsJson;
        paramsJson["fov"] = snapshot.parameters.fov;
        paramsJson["nearClip"] = snapshot.parameters.nearClip;
        paramsJson["farClip"] = snapshot.parameters.farClip;
        paramsJson["aspectRatio"] = snapshot.parameters.aspectRatio;
        jsonData["parameters"] = paramsJson;

        return jsonData;
    }

    CameraSnapshot CameraKeyframeEditorModule::JsonToSnapshot(const json& jsonData) const
    {
        CameraSnapshot snapshot{};
        snapshot.isDebugCamera = JsonManager::SafeGet(jsonData, "isDebugCamera", false);

        if (snapshot.isDebugCamera) {
            snapshot.target = JsonManager::JsonToVector3(jsonData["target"]);
            snapshot.distance = JsonManager::SafeGet(jsonData, "distance", 20.0f);
            snapshot.pitch = JsonManager::SafeGet(jsonData, "pitch", 0.25f);
            snapshot.yaw = JsonManager::SafeGet(jsonData, "yaw", 3.14159265359f);
        } else {
            snapshot.position = JsonManager::JsonToVector3(jsonData["position"]);
            snapshot.rotation = JsonManager::JsonToVector3(jsonData["rotation"]);
            snapshot.scale = JsonManager::JsonToVector3(jsonData["scale"]);
        }

        if (jsonData.contains("parameters")) {
            const auto& params = jsonData["parameters"];
            snapshot.parameters.fov = JsonManager::SafeGet(params, "fov", 0.45f);
            snapshot.parameters.nearClip = JsonManager::SafeGet(params, "nearClip", 0.1f);
            snapshot.parameters.farClip = JsonManager::SafeGet(params, "farClip", 1000.0f);
            snapshot.parameters.aspectRatio = JsonManager::SafeGet(params, "aspectRatio", 0.0f);
        }

        return snapshot;
    }

    void CameraKeyframeEditorModule::RefreshClipFileList()
    {
        clipFileList_.clear();

        if (std::filesystem::exists(clipDirectoryPath_)) {
            for (const auto& entry : std::filesystem::directory_iterator(clipDirectoryPath_)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    clipFileList_.push_back(entry.path().filename().string());
                }
            }

            std::sort(clipFileList_.begin(), clipFileList_.end());
        }

        needRefreshClipFileList_ = false;
    }

    bool CameraKeyframeEditorModule::SaveCurrentClipToFile(const std::string& filePath) const
    {
        json root;
        root["version"] = "1.0";
        root["timelineLength"] = timelineLength_;
        root["easingTypeIndex"] = easingTypeIndex_;

        json keyframesJson = json::array();
        for (const auto& key : keyframes_) {
            json keyJson;
            keyJson["time"] = key.time;
            keyJson["snapshot"] = SnapshotToJson(key.snapshot);
            keyframesJson.push_back(keyJson);
        }
        root["keyframes"] = keyframesJson;

        return JsonManager::GetInstance().SaveJson(filePath, root);
    }

    bool CameraKeyframeEditorModule::LoadClipFromFile(const std::string& filePath)
    {
        if (!JsonManager::GetInstance().FileExists(filePath)) {
            return false;
        }

        json root = JsonManager::GetInstance().LoadJson(filePath);
        if (root.empty()) {
            return false;
        }

        timelineLength_ = JsonManager::SafeGet(root, "timelineLength", 10.0f);
        easingTypeIndex_ = JsonManager::SafeGet(root, "easingTypeIndex", 0);

        keyframes_.clear();
        if (root.contains("keyframes") && root["keyframes"].is_array()) {
            for (const auto& keyJson : root["keyframes"]) {
                Keyframe key{};
                key.time = JsonManager::SafeGet(keyJson, "time", 0.0f);
                if (keyJson.contains("snapshot")) {
                    key.snapshot = JsonToSnapshot(keyJson["snapshot"]);
                }
                keyframes_.push_back(key);
            }
        }

        std::sort(keyframes_.begin(), keyframes_.end(),
            [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });

        selectedIndex_ = keyframes_.empty() ? -1 : 0;
        playhead_ = 0.0f;
        isPlaying_ = false;
        return true;
    }

    CameraKeyframeEditorModule::EditorState CameraKeyframeEditorModule::CaptureEditorState() const
    {
        EditorState state{};
        state.keyframes = keyframes_;
        state.timelineLength = timelineLength_;
        state.playhead = playhead_;
        state.selectedIndex = selectedIndex_;
        state.isPlaying = isPlaying_;
        state.loopPlayback = loopPlayback_;
        state.playbackSpeed = playbackSpeed_;
        state.easingTypeIndex = easingTypeIndex_;
        return state;
    }

    void CameraKeyframeEditorModule::ApplyEditorState(const EditorState& state)
    {
        keyframes_ = state.keyframes;
        timelineLength_ = state.timelineLength;
        playhead_ = state.playhead;
        selectedIndex_ = state.selectedIndex;
        isPlaying_ = state.isPlaying;
        loopPlayback_ = state.loopPlayback;
        playbackSpeed_ = state.playbackSpeed;
        easingTypeIndex_ = state.easingTypeIndex;
    }

    void CameraKeyframeEditorModule::PushUndoState()
    {
        undoStack_.push_back(CaptureEditorState());
        if (undoStack_.size() > maxHistoryCount_) {
            undoStack_.erase(undoStack_.begin());
        }
        redoStack_.clear();
    }

    void CameraKeyframeEditorModule::Undo()
    {
        if (undoStack_.empty()) {
            return;
        }

        redoStack_.push_back(CaptureEditorState());
        ApplyEditorState(undoStack_.back());
        undoStack_.pop_back();
    }

    void CameraKeyframeEditorModule::Redo()
    {
        if (redoStack_.empty()) {
            return;
        }

        undoStack_.push_back(CaptureEditorState());
        ApplyEditorState(redoStack_.back());
        redoStack_.pop_back();
    }
}

#endif // _DEBUG
