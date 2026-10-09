#pragma once

#include "Utility/Lifetime/ScopedRegistration.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace CoreEngine::Editor
{
    /// @brief メインメニューへ足す項目 1 つ
    struct EditorMenuItemDesc
    {
        /// @brief 「Tools/マップ生成」のように `/` で区切った場所（最初が一番上のメニュー、最後が項目の名前）
        std::string path;

        /// @brief 選んだときの処理
        std::function<void()> action;

        /// @brief チェックの印を付けるか（nullptr なら付けない）
        std::function<bool()> checked;
    };

    /// @brief エンジンの外（スクリプトなど）からメインメニューへ足す項目を集める
    class EditorMenuRegistry
    {
    public:
        static EditorMenuRegistry& Get();

        /// @brief 項目を登録する
        /// @return 登録を握るハンドル。破棄すると外れる
        /// @note 場所が `/` を含まない・区切りの間が空のものは登録しない。
        ScopedRegistration Register(EditorMenuItemDesc desc);

        /// @brief 一番上のメニュー root に登録された項目を、区切り線の後ろへ描く
        /// @note root の BeginMenu の中から呼ぶ。
        void DrawItems(std::string_view root);

        /// @brief builtInRoots に無い一番上のメニューを、登録された順に描く
        /// @note メインメニューバーの中から呼ぶ。
        void DrawExtraMenus(std::span<const std::string_view> builtInRoots);

    private:
        EditorMenuRegistry() = default;
        ~EditorMenuRegistry() = default;
        EditorMenuRegistry(const EditorMenuRegistry&) = delete;
        EditorMenuRegistry& operator=(const EditorMenuRegistry&) = delete;

        struct Entry
        {
            EditorMenuItemDesc desc;
            std::vector<std::string> segments;
            uint64_t registration = 0;
        };

        /// @brief depth 段目より下の項目を描く（同じ名前のサブメニューは最初に出た位置へまとめる）
        void DrawLevel(const std::vector<const Entry*>& entries, std::size_t depth);

        /// @brief 描いている間に選ばれた項目の処理を呼ぶ
        void RunPendingAction();

        std::vector<Entry> entries_;
        uint64_t lastRegistration_ = 0;
        std::function<void()> pendingAction_;
    };
}
