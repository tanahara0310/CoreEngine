#pragma once

#include "Camera/Debug/DebugCameraState.h"
#include "EngineSystem/Settings/IEditorSettingsSection.h"

namespace CoreEngine::Editor
{
    /// @brief エディタ視点カメラの設定・姿勢を次の起動へ持ち越す
    /// @details 控えは `DebugCameraState` が持ち、このセクションはそれを読み書きするだけ。
    class SceneCameraSection final : public IEditorSettingsSection
    {
    public:
        const char* GetSectionName() const override { return "SceneCamera"; }

        void Serialize(nlohmann::json& out) const override { DebugCameraState::Save(out); }
        void Deserialize(const nlohmann::json& in) override { DebugCameraState::Load(in); }

        /// @brief 個人の作業状態なので Saved/ 側へ置く
        StorageArea GetStorageArea() const override { return StorageArea::UserSaved; }

        /// @brief 控えの通番で変更を見る
        ChangeSignal GetChangeSignal() const override { return ChangeSignal::Revision; }
        uint64_t GetChangeRevision() const override { return DebugCameraState::GetRevision(); }
    };
}
