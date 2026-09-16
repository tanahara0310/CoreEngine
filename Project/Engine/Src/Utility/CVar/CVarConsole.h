#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// @file
/// @brief 文字列で CVar を読み書きするコマンドの層

namespace CoreEngine
{
    class ICVar;

    namespace CVarConsole
    {
        /// @brief コマンドの結果
        struct Result
        {
            bool handled = false;           ///< この層が扱うコマンドだったか
            bool failed = false;            ///< 扱ったが失敗したか
            std::vector<std::string> lines; ///< 表示する行
        };

        /// @brief 文字列を CVar へ書き込む（Undo へ積み、編集の確定として通知する）
        /// @param text 型に合わせた値（bool は true / false / on / off / 1 / 0、ベクタと色は空白かカンマ区切り）
        /// @param reason 書けなかった理由
        /// @return 書けたら true
        bool SetFromString(ICVar& cvar, std::string_view text, std::string& reason);

        /// @brief `cvar` で始まるコマンドを実行する
        /// @details `cvar`（使い方）／`cvar 接頭辞`（一覧）／`cvar 名前`（詳しく見る）／
        ///          `cvar 名前 値`（書き込む）／`cvar reset 名前`（既定値へ戻す）
        Result Execute(std::string_view commandLine);

        /// @brief 入力の続きとして使える候補（`cvar 名前` の形の行）
        /// @param limit 返す数の上限
        std::vector<std::string> Complete(std::string_view commandLine, std::size_t limit);
    }
}
