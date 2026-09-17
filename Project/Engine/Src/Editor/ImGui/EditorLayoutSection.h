#pragma once

#ifdef CORE_EDITOR

#include "EngineSystem/Settings/IEditorSettingsSection.h"

#include <string>

namespace CoreEngine
{
    class DockingUI;
    class GameDebugUI;
    class ProjectView;
}

namespace CoreEngine::Editor
{
    /// @brief エディタの画面の配置を次の起動へ持ち越す
    /// @details ドックの配置とウィンドウの位置・大きさ（ImGui の設定）、常設ウィンドウの開閉、
    ///          Project で開いているフォルダと表示形式を保存する。
    ///          保存が無ければ、DockingUI が標準の配置を組む。
    class EditorLayoutSection final : public IEditorSettingsSection
    {
    public:
        /// @param projectView Project のウィンドウ（無ければ nullptr）
        EditorLayoutSection(GameDebugUI& gameDebugUI, DockingUI& dockingUI, ProjectView* projectView);

        const char* GetSectionName() const override { return "EditorLayout"; }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;

        /// @brief 個人の作業状態なので Saved/ 側へ置く
        StorageArea GetStorageArea() const override { return StorageArea::UserSaved; }

    private:
        GameDebugUI* gameDebugUI_ = nullptr;
        DockingUI* dockingUI_ = nullptr;
        ProjectView* projectView_ = nullptr;

        /// @brief 最後に書き出した ImGui の設定（ImGui を片付けた後の保存にも使う）
        mutable std::string lastImGuiSettings_;
    };
}

#endif // CORE_EDITOR
