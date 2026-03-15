#include "CameraSnapshotEditorModule.h"

#ifdef _DEBUG

#include <imgui.h>

#include "Camera/CameraManager.h"
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Release/Camera.h"

namespace CoreEngine
{
    void CameraSnapshotEditorModule::Update(const CameraEditorContext& context)
    {
        // 初期版では再生や補間は持たず、UI操作のみ扱う。
        (void)context;
    }

    void CameraSnapshotEditorModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) {
            return;
        }

        ImGui::Text("アクティブ3D: %s", context.cameraManager->GetActiveCameraName(CameraType::Camera3D).c_str());
        ImGui::Separator();

        // スナップショット名を入力して現在カメラを保存する。
        ImGui::InputText("スナップショット名", snapshotNameBuffer_, sizeof(snapshotNameBuffer_));
        if (ImGui::Button("現在カメラを保存")) {
            CameraSnapshot snapshot;
            if (CaptureFromActiveCamera(context, snapshot)) {
                if (snapshotNameBuffer_[0] != '\0') {
                    snapshot.name = snapshotNameBuffer_;
                }
                snapshots_.push_back(snapshot);
                selectedIndex_ = static_cast<int>(snapshots_.size()) - 1;
            }
        }

        ImGui::Separator();

        if (snapshots_.empty()) {
            ImGui::TextDisabled("スナップショットがありません。");
            return;
        }

        // 登録済みスナップショット一覧を表示し、選択中を復元対象にする。
        if (ImGui::BeginListBox("Snapshots", ImVec2(-1.0f, 180.0f))) {
            for (int i = 0; i < static_cast<int>(snapshots_.size()); ++i) {
                const bool isSelected = (selectedIndex_ == i);
                const std::string label = snapshots_[i].name.empty()
                    ? ("スナップショット_" + std::to_string(i))
                    : snapshots_[i].name;

                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selectedIndex_ = i;
                }
            }
            ImGui::EndListBox();
        }

        const bool canRestore = (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(snapshots_.size()));
        if (canRestore && ImGui::Button("選択を復元")) {
            ApplyToActiveCamera(context, snapshots_[selectedIndex_]);
        }

        ImGui::SameLine();
        if (canRestore && ImGui::Button("選択を削除")) {
            snapshots_.erase(snapshots_.begin() + selectedIndex_);
            if (snapshots_.empty()) {
                selectedIndex_ = -1;
            } else if (selectedIndex_ >= static_cast<int>(snapshots_.size())) {
                selectedIndex_ = static_cast<int>(snapshots_.size()) - 1;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("すべて削除")) {
            snapshots_.clear();
            selectedIndex_ = -1;
        }
    }

    bool CameraSnapshotEditorModule::CaptureFromActiveCamera(const CameraEditorContext& context, CameraSnapshot& outSnapshot) const
    {
        ICamera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (!active3D) {
            return false;
        }

        if (auto* releaseCamera = dynamic_cast<Camera*>(active3D)) {
            outSnapshot = releaseCamera->CaptureSnapshot(snapshotNameBuffer_);
            return true;
        }

        if (auto* debugCamera = dynamic_cast<DebugCamera*>(active3D)) {
            outSnapshot = debugCamera->CaptureSnapshot(snapshotNameBuffer_);
            return true;
        }

        return false;
    }

    bool CameraSnapshotEditorModule::ApplyToActiveCamera(const CameraEditorContext& context, const CameraSnapshot& snapshot) const
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
