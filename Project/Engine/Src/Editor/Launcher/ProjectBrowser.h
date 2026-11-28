#pragma once

#ifdef CORE_EDITOR

#include "Editor/Launcher/ProjectCreator.h"
#include "Editor/Launcher/ProjectList.h"

#include <Windows.h>
#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace CoreEngine::Editor
{
    class EditorLogo;
    class ProjectThumbnails;

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

        /// @brief サムネイルを出すときに使う（無ければ色の面と頭文字を出す）
        void SetThumbnails(ProjectThumbnails* thumbnails) { thumbnails_ = thumbnails; }

        /// @brief 左の列の上に出すロゴ（無ければ名前の文字を出す）
        void SetLogo(EditorLogo* logo) { logo_ = logo; }

        /// @brief 削除の確認窓を出しているか、ごみ箱へ移している途中か（その間は画面を閉じない）
        bool IsBusy() const;

    private:
        /// @brief 右側に出している画面
        enum class View { Projects, NewProject };

        /// @brief 押された操作（描き終えてから行う）
        enum class Action { None, Open, ShowInExplorer, Remove, Delete, Add, Create, BrowseLocation };

        /// @brief 削除の確認窓で消そうとしているプロジェクト
        struct DeleteTarget
        {
            ProjectEntry entry;
            std::uintmax_t fileCount = 0; ///< フォルダの中のファイルの数
            std::uintmax_t totalBytes = 0; ///< フォルダの中のファイルの大きさの合計
        };

        void Refresh(const std::filesystem::path& select = {});
        std::vector<int> VisibleIndices() const;
        bool IsCurrent(const ProjectEntry& entry) const;
        ImTextureID ThumbnailOf(const ProjectEntry& entry) const;

        void DrawNav();
        void DrawProjects();
        void DrawList(const std::vector<int>& visible, const ImVec2& size);
        void DrawDetail(const ImVec2& size);
        void DrawNewProject();
        void DrawTemplates(const ImVec2& size);
        void DrawCreateForm(const ImVec2& size);
        void DrawStatus();
        void DrawDeleteDialog();
        void Request(Action action, const std::filesystem::path& folder);
        void RunPendingAction();

        void Open(const std::filesystem::path& folder);
        void ShowInExplorer(const std::filesystem::path& folder);
        void Remove(const std::filesystem::path& folder);
        void AddExisting(const std::filesystem::path& folder);
        void CreateProject();

        /// @brief 削除できないときの訳（削除できれば空）
        std::string DeleteBlocker(const ProjectEntry& entry) const;

        /// @brief 削除の確認窓を開く
        void ConfirmDelete(const std::filesystem::path& folder);

        /// @brief 確かめが済んだプロジェクトを、別のスレッドでごみ箱へ移し始める
        void StartDeleting();

        /// @brief ごみ箱へ移し終えていれば、一覧と下端の一言に結果を出す
        void FinishDeleting();

        /// @brief フォルダを選ぶ窓を出す（選んだフォルダは FinishPicking で受け取る）
        void StartPicking(Action purpose, const wchar_t* title);

        /// @brief フォルダを選ぶ窓が閉じていれば、選んだフォルダを使う
        void FinishPicking();

        ProjectList& list_;
        HWND owner_ = nullptr;
        std::filesystem::path current_;
        ProjectThumbnails* thumbnails_ = nullptr;
        EditorLogo* logo_ = nullptr;
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
        std::optional<DeleteTarget> deleteTarget_;    ///< 削除の確認窓で消そうとしているもの（窓を出している間だけある）
        bool openDeleteDialog_ = false;               ///< 次に描くときに削除の確認窓を開く
        char deleteConfirm_[128] = {};                ///< 確かめのために打ち込んだ名前
        std::future<bool> deleting_;                  ///< ごみ箱へ移す処理（移している間だけ有効。途中でやめたら true）
        ImVec2 dialogCenter_{};                       ///< 確認窓を置く位置（画面の真ん中）
        std::filesystem::path chosen_;
    };
}

#endif // CORE_EDITOR
