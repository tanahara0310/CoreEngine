#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace CoreEngine
{
    /// @brief オブジェクトに付けるタグの一覧
    /// @details 名前はプロジェクト設定（`Application/Config/EngineSettings/Tags.json`）が持つ。
    ///          初回の参照でファイルを読み、無ければ既定の並びを使う。
    /// @note エンジンが名指しするのは `Untagged` だけ。
    namespace GameObjectTags
    {
        /// @brief タグを付けていないことを表す名前（一覧の添字 0）
        inline constexpr const char* kUntagged = "Untagged";

        /// @brief タグの一覧（添字 0 は必ず Untagged）
        const std::vector<std::string>& Names();

        /// @brief 一覧にあるタグか
        bool IsDefined(std::string_view tag);

        /// @brief 一覧を決めて保存する
        /// @param names 添字 0 は必ず "Untagged"。空文字・重複は断る
        /// @param outError 断った訳（省略可）
        /// @return 保存できたら true
        bool SetNames(std::vector<std::string> names, std::string* outError = nullptr);

        /// @brief ファイルから読み直す
        void Reload();
    }
}
