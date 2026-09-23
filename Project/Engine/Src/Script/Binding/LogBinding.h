#pragma once

class asIScriptEngine;

namespace CoreEngine::Script
{
    /// @brief ログ出力の関数をスクリプトへ登録する
    /// @details `Log` / `Warn` / `Error` を登録し、どれも Script カテゴリへ出す。
    /// @return すべて登録できたら true
    bool RegisterLogBinding(asIScriptEngine* engine);
}
