#pragma once

#ifdef CORE_EDITOR

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CoreEngine::Editor::SceneNameList
{
    /// @brief シーンの一覧の 1 行（群か、群に入らない 1 つのシーン）
    struct Entry
    {
        std::string group;               ///< 群の名前（群でなければ空）
        std::vector<std::string> scenes; ///< 群のシーン（名前順）。群でなければ 1 つ

        bool IsGroup() const { return !group.empty(); }
    };

    /// @brief 名前の最初の `_` より前（`_` が無いか先頭なら空）
    std::string_view Prefix(std::string_view sceneName);

    /// @brief 群の中で見せる名前（先頭の「群の名前_」を外したもの。外せないか外すと空になるなら元の名前）
    std::string_view ShortName(std::string_view sceneName, std::string_view group);

    /// @brief シーンを群に分ける
    /// @details 名前の最初の `_` より前が同じシーンが 2 つ以上あれば 1 つの群にまとめる。
    ///          群と、群に入らないシーンを、大文字と小文字を区別しない名前順で並べる。
    std::vector<Entry> Group(const std::vector<std::string>& sceneNames);

    /// @brief 名前が絞り込みの字に当たったときの結果
    struct Match
    {
        float score = 0.0f;                                   ///< 大きいほど上に並べる
        std::vector<std::pair<std::size_t, std::size_t>> ranges; ///< 当たった字のバイトの範囲（始まりと長さ）
    };

    /// @brief 名前が絞り込みの字に当たるかを調べる
    /// @details 英字の大文字と小文字は区別しない。空白は無視する。字が続いていなくても、順に含んでいれば当たる。
    /// @return 当たらなければ値なし（絞り込みの字が空なら score 0 で当たる）
    std::optional<Match> Find(std::string_view query, std::string_view sceneName);

    /// @brief 絞り込みに当たったシーン
    struct Hit
    {
        std::string name;
        Match match;
    };

    /// @brief 当たったシーンを、よく当たった順（同じなら名前順）に集める
    std::vector<Hit> Search(std::string_view query, const std::vector<std::string>& sceneNames);

    /// @brief 絞り込みの字が空白だけか
    bool IsBlank(std::string_view query);
}

#endif // CORE_EDITOR
