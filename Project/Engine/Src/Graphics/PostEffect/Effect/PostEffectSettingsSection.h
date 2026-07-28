#pragma once

#include "EngineSystem/Settings/IEditorSettingsSection.h"

/// @brief ポストエフェクトのエディタ設定自動保存セクション

namespace CoreEngine
{
    class EngineSystem;

    /// @brief 全ポストエフェクトの有効状態とパラメータの自動保存セクション
    /// @details シリアライズ処理は PostEffectPresetManager の CaptureToJson / ApplyFromJson を
    ///          共用する（プリセットの明示保存と同じ範囲が自動保存される）。
    ///          オーナーはエンジン常駐の PostEffectManager のため、エンジン寿命で
    ///          DebugSubsystem が登録する。
    ///          FadeEffect の fadeAlpha は実行時のシーン遷移状態のため保存対象外
    ///          （保存すると次回起動が黒画面で始まる事故になる）。
    class PostEffectSettingsSection : public IEditorSettingsSection
    {
    public:
        explicit PostEffectSettingsSection(EngineSystem* engine) : engine_(engine) {}

        const char* GetSectionName() const override { return "PostEffects"; }
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;

    private:
        EngineSystem* engine_ = nullptr;
    };
}
