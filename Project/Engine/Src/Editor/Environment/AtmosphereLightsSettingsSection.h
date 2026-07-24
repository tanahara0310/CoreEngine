#pragma once

#include "EngineSystem/Settings/IEditorSettingsSection.h"

/// @brief 大気の太陽・月ライトのエディタ設定自動保存セクション

namespace CoreEngine
{
    class EngineSystem;

    /// @brief 大気の太陽・月ライト状態の自動保存セクション
    /// @details 太陽・月ライトはシーン寿命のため、EnvironmentFeature が
    ///          PostSceneInitialize（ライト生成後）で登録し Finalize で解除する。
    ///          方向は生ベクトルで保存する（高度角/方位角への変換は AtmosphereEditor の
    ///          SyncFromLights が UI 表示用に行うため、ここでは不要）。
    ///          太陽のサーフェス照度・色は Lighting エディタ側の責務のため保存対象外。
    ///          大気の物理パラメータ（エンジン寿命）は AtmosphereSettingsSection が扱う。
    class AtmosphereLightsSettingsSection : public IEditorSettingsSection
    {
    public:
        explicit AtmosphereLightsSettingsSection(EngineSystem* engine) : engine_(engine) {}

        const char* GetSectionName() const override { return "AtmosphereLights"; }
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;

    private:
        EngineSystem* engine_ = nullptr;
    };
}
