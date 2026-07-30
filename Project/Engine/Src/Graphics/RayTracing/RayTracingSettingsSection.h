#pragma once

#include "EngineSystem/Settings/IEditorSettingsSection.h"

/// @brief レイトレーシング設定のエディタ設定自動保存セクション

namespace CoreEngine
{
    class EngineSystem;

    /// @brief RT シャドウのチューニング値の自動保存セクション
    /// @details オーナーは RenderDomainContext 所有の RayTracingShadowManager（エンジン寿命）のため、
    ///          DebugSubsystem がエンジン寿命で登録する。
    ///          Stage 0 の目的の一つは「設定が到達可能になること」で、
    ///          それまで RayTracingShadowSettings::SetSettings は呼び出し元がゼロだった。
    ///          設計書: Docs/Engine/RayTracing/RayTracing_Refactoring_Review.md
    class RayTracingSettingsSection : public IEditorSettingsSection
    {
    public:
        explicit RayTracingSettingsSection(EngineSystem* engine) : engine_(engine) {}

        const char* GetSectionName() const override { return "RayTracing"; }
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;

    private:
        EngineSystem* engine_ = nullptr;
    };
}
