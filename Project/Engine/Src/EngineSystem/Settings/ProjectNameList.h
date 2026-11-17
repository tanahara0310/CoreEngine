#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace CoreEngine
{
    /// @brief プロジェクト設定の「名前の並び」（当たり判定のレイヤー・タグなど）を 1 つのファイルで読み書きする
    /// @details ファイルは `{"version": "1.0", "<key>": ["先頭の名前", …]}` の形。
    ///          先頭は決まった名前で、空の名前・重なり・上限を超える数は持てない。
    /// @note 作った時点でファイルを読む。ログの初期化より前に作られうるので、読み込みはログを使わない。
    class ProjectNameList
    {
    public:
        /// @brief 並びの決まり
        struct Rules
        {
            const char* settingsPath = "";        ///< プロジェクトの根からのファイルの場所
            const char* key = "";                 ///< 並びを入れるキー
            const char* noun = "";                ///< 断る訳とログに使う呼び名（「レイヤー」など）
            const char* first = "";               ///< 先頭に必ず置く名前
            std::size_t maxCount = 0;             ///< 持てる数の上限（0 なら上限なし）
            std::vector<std::string> defaults;    ///< ファイルが無い・壊れているときの並び
        };

        explicit ProjectNameList(Rules rules);

        /// @brief 名前の並び（添字 0 は必ず先頭の名前）
        const std::vector<std::string>& Names() const { return names_; }

        /// @brief 並びにある名前か
        bool Contains(std::string_view name) const;

        /// @brief 名前の並びとして成り立っているか
        /// @param outError 断った訳（省略可）
        bool Validate(const std::vector<std::string>& names, std::string* outError = nullptr) const;

        /// @brief 名前を決めてファイルへ保存する
        /// @param outError 断った訳（省略可）
        /// @return 保存できたら true（成り立たない並びは保存しない）
        bool SetNames(std::vector<std::string> names, std::string* outError = nullptr);

        /// @brief ファイルから読み直す
        void Reload();

    private:
        std::vector<std::string> Read() const;
        bool Write(const std::vector<std::string>& names) const;

        Rules rules_;
        std::vector<std::string> names_;
    };
}
