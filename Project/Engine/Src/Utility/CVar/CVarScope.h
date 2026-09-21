#pragma once

#include <string_view>
#include <vector>

/// @file
/// @brief CVar の値を持つ相手（プロジェクト / シーン）の区別

namespace CoreEngine
{
    class ICVar;

    /// @brief CVar の値を持つ相手
    /// @details 保存も読み込みもこの区別で絞るので、同じ CVar が 2 つのファイルへ
    ///          書かれることがない。片方のファイルにもう片方の値が紛れ込んでいても当たらない。
    enum class CVarScope
    {
        /// @brief プロジェクトが持つ（Config/EngineSettings/CVars.json と Saved/EditorState.json）
        Project,

        /// @brief シーンが持つ（Assets/Scenes/<名前>/_environment.json）
        Scene,

        /// @brief 持ち主で絞らない
        /// @details プリセットのように「ある系統を丸ごと写す」用途で使う。
        Any,
    };

    /// @brief 持ち主の判定
    namespace CVarScopes
    {
        /// @brief シーンが持つ系統の名前を返す
        /// @details 系統ごと移すこと。同じ系統の中で持ち主が分かれると、
        ///          どちらのファイルに入るかが名前から読めなくなる。
        ///          中身はシーンの画を決めるもの（環境とポストエフェクト）。
        std::vector<std::string_view> SceneOwnedPrefixes();

        /// @brief その名前の CVar をシーンが持つか
        bool IsSceneOwned(std::string_view name);

        /// @brief その CVar が指定の持ち主のものか
        bool Matches(const ICVar& cvar, CVarScope scope);
    }
}
