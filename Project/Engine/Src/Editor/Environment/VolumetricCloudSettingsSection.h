#pragma once

#include "EngineSystem/Settings/IEditorSettingsSection.h"

/// @brief ボリューメトリック雲のエディタ設定自動保存セクション

namespace CoreEngine
{
    class EngineSystem;
    class VolumetricCloudEditor;

    /// @brief 雲パラメータ（VolumetricCloudParameters）と有効トグル・プリセット index の自動保存セクション
    /// @details オーナーはエンジン常駐の VolumetricCloudManager のため、エンジン寿命で
    ///          DebugSubsystem が登録する。復元はパラメータ値をそのまま適用し、
    ///          プリセット index は UI 表示状態としてのみ復元する
    ///          （プリセット定義が将来変わっても実パラメータが優先される）。
    class VolumetricCloudSettingsSection : public IEditorSettingsSection
    {
    public:
        /// @param engine エンジンシステム
        /// @param editor プリセット index の保持元エディタ（本セクションより長く生存させること）
        VolumetricCloudSettingsSection(EngineSystem* engine, VolumetricCloudEditor* editor)
            : engine_(engine), editor_(editor) {}

        const char* GetSectionName() const override { return "VolumetricCloud"; }
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;

    private:
        EngineSystem* engine_ = nullptr;
        VolumetricCloudEditor* editor_ = nullptr;
    };
}
