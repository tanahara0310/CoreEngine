#pragma once

#include "EngineSystem/Settings/IEditorSettingsSection.h"

/// @brief レンダリング技術のエディタ設定自動保存セクション

namespace CoreEngine
{
    class EngineSystem;

    /// @brief レンダリング技術（SSAO 等）の有効状態とチューニング値の自動保存セクション
    /// @details オーナーはエンジン常駐の RenderingTechniqueManager のため、エンジン寿命で
    ///          DebugSubsystem が登録する。
    ///          - 常時有効の技術（DeferredLighting 等）の enabled は復元時に変更しない
    ///          - WaterCaustics のパラメータは Water セクション（WaterEditorFacade）が所有するため
    ///            ここでは enabled のみ扱う（二重管理の防止）
    ///          - 行列・画面サイズなど毎フレーム自動設定されるフィールドは保存対象外
    class RenderingTechniqueSettingsSection : public IEditorSettingsSection
    {
    public:
        explicit RenderingTechniqueSettingsSection(EngineSystem* engine) : engine_(engine) {}

        const char* GetSectionName() const override { return "RenderingTechniques"; }
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;

    private:
        EngineSystem* engine_ = nullptr;
    };
}
