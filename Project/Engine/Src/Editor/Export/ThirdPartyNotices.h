#pragma once

#ifdef CORE_EDITOR

#include <string>
#include <string_view>

namespace CoreEngine::Editor
{
    /// @brief 書き出したゲームに入れる外部ライブラリの表記（THIRD_PARTY_NOTICES.txt）
    /// @details 外部ライブラリの一覧（エンジンの根の Externals/ThirdParty.json）から、
    ///          inGame が true のものの名前・版・取得元・ライセンスと、licenseFiles の本文を並べる。
    namespace ThirdPartyNotices
    {
        /// @brief 書き出す先の根に置くファイルの名前
        inline constexpr std::string_view kFileName = "THIRD_PARTY_NOTICES.txt";

        /// @brief 表記の本文を作る（改行は CRLF・UTF-8）
        /// @param error 作れなかった訳（作れたら空）
        /// @return 表記の本文（作れなければ空）
        std::string Build(std::string& error);
    }
}

#endif // CORE_EDITOR
