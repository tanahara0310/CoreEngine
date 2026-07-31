#pragma once

#include "EngineSystem/Settings/IEditorSettingsSection.h"

/// @brief DebugCamera のエディタ設定自動保存セクション

namespace CoreEngine
{
    class DebugCamera;

    /// @brief DebugCamera の設定・姿勢を自動保存するセクションアダプタ
    /// @details DebugCamera 本体に nlohmann/json への依存を持ち込まないため、
    ///          シリアライズ処理をアダプタとして分離する。
    ///          保存対象: CameraSettings・姿勢（target/distance/pitch/yaw）・
    ///          投影パラメータ（fov/nearClip/farClip/aspectRatio）
    class DebugCameraSettingsSection : public IEditorSettingsSection
    {
    public:
        /// @param camera 対象カメラ（本セクションより長く生存させること）
        explicit DebugCameraSettingsSection(DebugCamera* camera) : camera_(camera) {}

        const char* GetSectionName() const override { return "DebugCamera"; }
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;

    private:
        DebugCamera* camera_ = nullptr;
    };
}
