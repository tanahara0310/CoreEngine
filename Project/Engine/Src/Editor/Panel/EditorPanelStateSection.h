#pragma once

#include "EngineSystem/Settings/IEditorSettingsSection.h"

namespace CoreEngine::Editor
{
    /// @brief 開いているパネルを次の起動へ持ち越す
    /// @details 単独ウィンドウと Inspector タブの開閉を保存する。
    ///          これが無いと、起動のたびに開き直しになる。
    class EditorPanelStateSection final : public IEditorSettingsSection
    {
    public:
        const char* GetSectionName() const override { return "EditorPanels"; }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;

        /// @brief 個人の作業状態なので Saved/ 側へ置く
        StorageArea GetStorageArea() const override { return StorageArea::UserSaved; }
    };
}
