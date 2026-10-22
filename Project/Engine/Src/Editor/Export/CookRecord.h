#pragma once

#ifdef CORE_EDITOR

#include <string>

namespace CoreEngine::Editor
{
    /// @brief クックした派生データ 1 つの記録（クックの一覧に書く）
    struct CookRecord
    {
        std::string source;   ///< 元のファイルの綴り
        std::string guid;     ///< 元のファイルの GUID（無ければ空）
        std::string derived;  ///< 書いた派生データの綴り
        std::string settings; ///< 変換の設定
        std::string key;      ///< 派生キー
    };
}

#endif // CORE_EDITOR
