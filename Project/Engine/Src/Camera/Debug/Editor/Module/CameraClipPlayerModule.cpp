#include "CameraClipPlayerModule.h"
#include "CameraSequenceAssetIO.h"

#ifdef _DEBUG

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

#include "Camera/CameraManager.h"
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Release/Camera.h"

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

    void CameraClipPlayerModule::Update(const CameraEditorContext& context)
    {
        if (!context.cameraManager || !isPlaying_ || clipKeyframes_.empty()) {
            return;
        }

        // 再生ヘッドを進め、シーケンスの補間結果を現在カメラに適用する。
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
    }

    void CameraClipPlayerModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) {
            return;
        }

        if (needRefreshClipFileList_) {
            RefreshClipFileList();
        }

        ImGui::Text("アクティブ3D: %s", context.cameraManager->GetActiveCameraName(CameraType::Camera3D).c_str());
        ImGui::Text("読み込み中シーケンス: %s", loadedClipName_.empty() ? "なし" : loadedClipName_.c_str());
        ImGui::TextDisabled("ここで読み込んだシーケンスがゲームカメラ再生データです。");
        ImGui::Separator();

        if (ImGui::Button("シーケンス一覧を更新")) {
            needRefreshClipFileList_ = true;
        }

        if (clipFileList_.empty()) {
            ImGui::TextDisabled("保存済みシーケンスがありません。");
        } else {
            selectedClipFileIndex_ = std::clamp(selectedClipFileIndex_, -1, static_cast<int>(clipFileList_.size()) - 1);

            if (ImGui::BeginListBox("保存済みシーケンス", ImVec2(-1.0f, 140.0f))) {
                for (int i = 0; i < static_cast<int>(clipFileList_.size()); ++i) {
                    const bool isSelected = (selectedClipFileIndex_ == i);
                    if (ImGui::Selectable(clipFileList_[i].c_str(), isSelected)) {
                        selectedClipFileIndex_ = i;
                    }
                }
                ImGui::EndListBox();
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

        ImGui::Separator();

        if (!statusMessage_.empty()) {
            ImGui::TextDisabled("%s", statusMessage_.c_str());
        }

        if (clipKeyframes_.empty()) {
            ImGui::TextDisabled("再生可能なシーケンスが読み込まれていません。");
            return;
        }

        ImGui::Text("キーフレーム数: %d", static_cast<int>(clipKeyframes_.size()));
        ImGui::Text("ショット数: %d (%s)", static_cast<int>(clipShots_.size()), shotsEnabled_ ? "有効" : "無効");
        ImGui::DragFloat("再生速度", &playbackSpeed_, 0.05f, 0.1f, 4.0f, "%.2fx");
        ImGui::Checkbox("ループ再生", &loopPlayback_);

        const char* easingLabel = (easingTypeIndex_ >= 0 && easingTypeIndex_ < kEasingOptionCount)
            ? kEasingOptions[easingTypeIndex_].label
            : "不明";
        ImGui::Text("補間タイプ: %s", easingLabel);

        bool playheadChanged = ImGui::SliderFloat("再生ヘッド", &playhead_, 0.0f, timelineLength_, "%.2f 秒");

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
            playheadChanged = true;
        }

        if (!isPlaying_ && playheadChanged) {
            CameraSnapshot evaluated{};
            if (EvaluateSnapshotAt(playhead_, evaluated)) {
                ApplyToActiveCamera(context, evaluated);
            }
        }
    }

    void CameraClipPlayerModule::RefreshClipFileList()
    {
        clipFileList_ = CameraSequenceAssetIO::GetSequenceFileList(clipDirectoryPath_);
        needRefreshClipFileList_ = false;
    }

    bool CameraClipPlayerModule::LoadClipFromFile(const std::string& filePath)
    {
        CameraSequenceAsset asset{};
        if (!CameraSequenceAssetIO::Load(filePath, asset)) {
            return false;
        }

        timelineLength_ = asset.timelineLength;
        easingTypeIndex_ = asset.easingTypeIndex;
        shotsEnabled_ = asset.shotsEnabled;
        if (timelineLength_ < 0.1f) {
            timelineLength_ = 0.1f;
        }

        clipKeyframes_.clear();
        for (const auto& key : asset.keyframes) {
            ClipKeyframe localKey{};
            localKey.time = key.time;
            localKey.snapshot = key.snapshot;
            clipKeyframes_.push_back(localKey);
        }

        std::sort(clipKeyframes_.begin(), clipKeyframes_.end(),
            [](const ClipKeyframe& a, const ClipKeyframe& b) { return a.time < b.time; });

        clipShots_.clear();
        for (const auto& shot : asset.shots) {
            ClipShot localShot{};
            localShot.name = shot.name;
            localShot.startTime = shot.startTime;
            localShot.endTime = shot.endTime;
            localShot.enabled = shot.enabled;
            localShot.transitionType = (shot.transitionType == CameraSequenceTransitionType::Blend)
                ? ShotTransitionType::Blend
                : ShotTransitionType::Cut;
            localShot.blendDuration = shot.blendDuration;
            clipShots_.push_back(localShot);
        }

        playhead_ = 0.0f;
        isPlaying_ = false;
        loadedClipName_ = std::filesystem::path(filePath).filename().string();
        statusMessage_.clear();
        return !clipKeyframes_.empty();
    }

    bool CameraClipPlayerModule::EvaluateSnapshotAt(float time, CameraSnapshot& outSnapshot) const
    {
        if (!EvaluateSnapshotRaw(time, outSnapshot)) {
            return false;
        }

        // ショット管理有効時は、ショット境界での遷移方式（カット/ブレンド）を適用する。
        if (!shotsEnabled_ || clipShots_.empty()) {
            return true;
        }

        const float clampedTime = std::clamp(time, 0.0f, timelineLength_);
        const int shotIndex = FindShotIndexAt(clampedTime);
        if (shotIndex < 0 || shotIndex >= static_cast<int>(clipShots_.size())) {
            return true;
        }

        const ClipShot& currentShot = clipShots_[shotIndex];
        if (!currentShot.enabled || currentShot.transitionType != ShotTransitionType::Blend) {
            return true;
        }

        int previousShotIndex = -1;
        for (int i = shotIndex - 1; i >= 0; --i) {
            if (clipShots_[i].enabled) {
                previousShotIndex = i;
                break;
            }
        }

        if (previousShotIndex < 0) {
            return true;
        }

        const ClipShot& previousShot = clipShots_[previousShotIndex];
        const float currentShotDuration = (std::max)(currentShot.endTime - currentShot.startTime, 0.0f);
        const float blendDuration = std::clamp(currentShot.blendDuration, 0.0f, currentShotDuration);
        if (blendDuration <= 0.0001f) {
            return true;
        }

        const float blendStart = currentShot.startTime;
        const float blendEnd = blendStart + blendDuration;
        if (clampedTime < blendStart || clampedTime > blendEnd) {
            return true;
        }

        CameraSnapshot fromSnapshot{};
        if (!EvaluateSnapshotRaw(previousShot.endTime, fromSnapshot)) {
            return true;
        }

        CameraSnapshot toSnapshot{};
        if (!EvaluateSnapshotRaw(clampedTime, toSnapshot)) {
            return true;
        }

        const float blendT = std::clamp((clampedTime - blendStart) / blendDuration, 0.0f, 1.0f);
        outSnapshot = InterpolateSnapshot(fromSnapshot, toSnapshot, blendT);
        return true;
    }

    bool CameraClipPlayerModule::EvaluateSnapshotRaw(float time, CameraSnapshot& outSnapshot) const
    {
        if (clipKeyframes_.empty()) {
            return false;
        }

        if (clipKeyframes_.size() == 1) {
            outSnapshot = clipKeyframes_.front().snapshot;
            return true;
        }

        const float clampedTime = std::clamp(time, 0.0f, timelineLength_);

        if (clampedTime <= clipKeyframes_.front().time) {
            outSnapshot = clipKeyframes_.front().snapshot;
            return true;
        }

        if (clampedTime >= clipKeyframes_.back().time) {
            outSnapshot = clipKeyframes_.back().snapshot;
            return true;
        }

        // 指定時刻が含まれる区間を見つけ、補間結果を返す。
        for (size_t i = 0; i + 1 < clipKeyframes_.size(); ++i) {
            const ClipKeyframe& from = clipKeyframes_[i];
            const ClipKeyframe& to = clipKeyframes_[i + 1];

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

        outSnapshot = clipKeyframes_.back().snapshot;
        return true;
    }

    CameraSnapshot CameraClipPlayerModule::InterpolateSnapshot(const CameraSnapshot& from, const CameraSnapshot& to, float t) const
    {
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

    EasingUtil::Type CameraClipPlayerModule::GetSelectedEasingType() const
    {
        if (easingTypeIndex_ < 0 || easingTypeIndex_ >= kEasingOptionCount) {
            return EasingUtil::Type::Linear;
        }

        return kEasingOptions[easingTypeIndex_].type;
    }

    int CameraClipPlayerModule::FindShotIndexAt(float time) const
    {
        int found = -1;
        for (int i = 0; i < static_cast<int>(clipShots_.size()); ++i) {
            const ClipShot& shot = clipShots_[i];
            if (!shot.enabled) {
                continue;
            }

            if (time >= shot.startTime && time <= shot.endTime) {
                found = i;
                break;
            }
        }

        return found;
    }

    bool CameraClipPlayerModule::ApplyToActiveCamera(const CameraEditorContext& context, const CameraSnapshot& snapshot) const
    {
        ICamera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (!active3D) {
            return false;
        }

        if (snapshot.isDebugCamera) {
            if (auto* debugCamera = dynamic_cast<DebugCamera*>(active3D)) {
                debugCamera->RestoreSnapshot(snapshot);
                return true;
            }
            return false;
        }

        if (auto* releaseCamera = dynamic_cast<Camera*>(active3D)) {
            releaseCamera->RestoreSnapshot(snapshot);
            return true;
        }

        return false;
    }
}

#endif // _DEBUG
