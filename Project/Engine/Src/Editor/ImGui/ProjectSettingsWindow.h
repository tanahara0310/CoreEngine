#pragma once

#ifdef USE_IMGUI

#include <cstddef>
#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief CVar と機能ごとの設定を 1 か所で触る Project Settings ウィンドウ
    /// @details 左のツリーは CVar の名前から作るので、宣言した CVar は必ずどこかに出る。
    class ProjectSettingsWindow
    {
    public:
        /// @brief ウィンドウを描く
        /// @param visible 表示中か（× で閉じると false になる）
        void Draw(bool& visible);

    private:
        /// @brief CVar のまとまり 1 つ（`r.Water.` など）
        struct Group
        {
            std::string prefix;     ///< 絞り込みに使う接頭辞（末尾は点）
            std::string label;      ///< ツリーに出す名前
            std::size_t count = 0;  ///< 項目の数（UI に出さないものを除く）
        };

        /// @brief まとまりの並び（ツリーの 1 つの見出しの中身）
        struct Family
        {
            std::string label;          ///< 見出し
            bool personal = false;      ///< 自分だけの設定（d.）か
            std::vector<Group> groups;
        };

        /// @brief 登録されている CVar からツリーの中身を作り直す
        void RebuildFamilies();

        void DrawTree();
        void DrawGroupPane();
        void DrawSectionPane();
        void DrawSearchResults();
        void DrawFooter();

        /// @brief 選んでいるまとまり（無ければ nullptr）
        const Group* FindSelectedGroup(bool* personal = nullptr) const;

        std::vector<Family> families_;
        std::size_t seenCVarCount_ = 0;

        std::string selectedPrefix_;    ///< 選んでいる CVar のまとまり
        std::string selectedSection_;   ///< 選んでいる機能の設定
        char filter_[64] = {};          ///< 名前の検索語
    };
}

#endif // USE_IMGUI
