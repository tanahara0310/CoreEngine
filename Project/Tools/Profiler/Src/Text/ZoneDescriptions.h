#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// @file ZoneDescriptions.h
/// @brief exe に埋め込んだ説明の辞書（Data/ZoneDescriptions.json）から、区間の「何をしているか」を引く

namespace CoreEngine::Profiler
{
    class ZoneDescriptions
    {
    public:
        /// @brief 辞書（初めて使うときに exe の中から読む）
        static const ZoneDescriptions& Instance();

        /// @brief 区間の説明。名前がそのまま同じ説明、無ければ形で当てた説明。どれにも当たらなければ空
        /// @param file 場所のファイル（フルパスでもファイル名でもよい）
        std::string Describe(std::string_view name, std::string_view file) const;

        /// @brief 「自分だけ」（子のどの区間にも入らない時間）の説明
        std::string DescribeSelf(std::string_view name) const;

        /// @brief 「仕事と待ち」の待ちに数える区間か
        bool IsWait(std::string_view name) const;

        /// @brief 辞書を読めたか（読めなければ説明は空・待ちは既定の 2 つ）
        bool IsLoaded() const { return loaded_; }

    private:
        struct Pattern
        {
            std::string match;
            std::string file; ///< 空ならファイルを問わない
            std::string text;
        };

        ZoneDescriptions();
        void Load(std::string_view json);

        std::unordered_map<std::string, std::string> names_;
        std::vector<Pattern> patterns_;
        std::unordered_map<std::string, std::string> self_;
        std::string selfDefault_;
        std::vector<std::string> waits_;
        bool loaded_ = false;
    };
}
