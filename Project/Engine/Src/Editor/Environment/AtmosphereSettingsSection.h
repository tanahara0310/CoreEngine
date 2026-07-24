#pragma once

#include "EngineSystem/Settings/IEditorSettingsSection.h"

/// @brief 大気散乱のエディタ設定自動保存セクション

namespace CoreEngine
{
    class EngineSystem;

    /// @brief 大気の物理パラメータ（AtmosphereParameters）と付随トグルの自動保存セクション
    /// @details オーナーはエンジン常駐の AtmosphereManager のため、エンジン寿命で
    ///          DebugSubsystem が登録する。太陽・月ライト（シーン寿命）は別セクション
    ///          （AtmosphereLightsSettingsSection）が扱う（二重オーナー対応）。
    ///          groundLevelY はシーン側で指定する値のため保存対象外
    ///          （エディタの「大気パラメータをリセット」がシーン設定として維持するのと同じ扱い）。
    class AtmosphereSettingsSection : public IEditorSettingsSection
    {
    public:
        explicit AtmosphereSettingsSection(EngineSystem* engine) : engine_(engine) {}

        const char* GetSectionName() const override { return "Atmosphere"; }
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;

    private:
        EngineSystem* engine_ = nullptr;
    };
}
