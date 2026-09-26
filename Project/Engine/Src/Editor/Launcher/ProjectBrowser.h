#pragma once

#ifdef CORE_EDITOR

#include "Editor/Launcher/ProjectCreator.h"
#include "Editor/Launcher/ProjectList.h"

#include <Windows.h>
#include <imgui.h>

#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace CoreEngine::Editor
{
    /// @brief プロジェクトの一覧と新規作成の画面
    /// @details ランチャー（エンジンの初期化の前）とエディタ（File メニュー）の両方で使う。
    ///          今の窓の中を埋めて描く。開くプロジェクトが決まったら TakeChosen で受け取る。
    class ProjectBrowser
    {
    public:
        /// @param list 一覧（呼ぶ側が持つ）
        /// @param owner フォルダを選ぶ窓の持ち主
        /// @param current 開いているプロジェクト（ランチャーでは空）
        ProjectBrowser(ProjectList& list, HWND owner, std::filesystem::path current = {});

        /// @brief 一覧の画面にする
        void ShowProjects();

        /// @brief 新規作成の画面にする
        void ShowNewProject();

        /// @brief 1 フレーム分を描き、押された操作を行う
        void Draw();

        /// @brief 開くと決まったプロジェクトを受け取る（無ければ空。受け取ると空に戻る）
        std::filesystem::path TakeChosen();

        /// @brief 下端の一言を変える
        void SetStatus(std::string text, bool error = false);

    private:
        /// @brief 右側に出している画面
        enum class View { Projects, NewProject };

        /// @brief 押された操作（描き終えてから行う）
        enum class Action { None, Open, ShowInExplorer, Remove, Add, Create, BrowseLocation };

        void Refresh(const std::filesystem::path& select = {});
        std::vector<int> VisibleIndices() const;
        bool IsCurrent(const ProjectEntry& entry) const;

        void DrawNav();
        void DrawProjects();
        void DrawList(const std::vector<int>& visible, const ImVec2& size);
        void DrawDetail(const ImVec2& size);
        void DrawNewProject();
        void DrawTemplates(const ImVec2& size);
        void DrawCreateForm(const ImVec2& size);
        void DrawStatus();
        void Request(Action action, const std::filesystem::path& folder);
        void RunPendingAction();

        void Open(const std::filesystem::path& folder);
        void ShowInExplorer(const std::filesystem::path& folder);
        void Remove(const std::filesystem::path& folder);
        void AddExisting(const std::filesystem::path& folder);
        void CreateProject();

        /// @brief フォルダを選ぶ窓を出す（選んだフォルダは FinishPicking で受け取る）
        void StartPicking(Action purpose, const wchar_t* title);

        /// @brief フォルダを選ぶ窓が閉じていれば、選んだフォルダを使う
        void FinishPicking();

        ProjectList& list_;
        HWND owner_ = nullptr;
        std::filesystem::path current_;
        std::string engineRootText_;
        View view_ = View::Projects;
        std::vector<ProjectEntry> entries_;
        int selected_ = -1;
        char filter_[128] = {};
        std::vector<ProjectTemplate> templates_;
        int selectedTemplate_ = 0;
        char name_[65] = {};
        char location_[1024] = {};
        std::string status_;
        bool statusError_ = false;
        Action pending_ = Action::None;
        std::filesystem::path pendingFolder_;
        std::future<std::filesystem::path> picking_; ///< フォルダを選ぶ窓（開いている間だけ有効）
        Action pickingFor_ = Action::None;            ///< 選んだフォルダを何に使うか
        std::filesystem::path chosen_;
    };
}

#endif // CORE_EDITOR
