#pragma once

#include "EngineSystem/Settings/IEditorSettingsSection.h"

/// @brief デバッグ用カメラ（軌道操作つき）のエディタ設定自動保存セクション

namespace CoreEngine
{
    class Camera;
    class OrbitFlyController;

    /// @brief 軌道操作カメラの設定・姿勢を自動保存するセクションアダプタ
    /// @details カメラ本体／コントローラに nlohmann/json への依存を持ち込まないため、
    ///          シリアライズ処理をアダプタとして分離する。
    ///          保存対象: OrbitFlyController::Settings・軌道状態（target/distance/pitch/yaw）・
    ///          投影パラメータ（fov/nearClip/farClip/aspectRatio）
    ///          JSON のキーは従来（DebugCamera 時代）と同じで、既存の保存ファイルを読み続けられる。
    class DebugCameraSettingsSection : public IEditorSettingsSection
    {
    public:
        /// @param camera 対象カメラ（本セクションより長く生存させること）
        /// @param controller 対象カメラに取り付けた軌道コントローラ
        DebugCameraSettingsSection(Camera* camera, OrbitFlyController* controller)
            : camera_(camera), controller_(controller) {}

        const char* GetSectionName() const override { return "DebugCamera"; }
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;

    private:
        Camera* camera_ = nullptr;
        OrbitFlyController* controller_ = nullptr;
    };
}
