#pragma once

#include <string>

class asIScriptContext;
struct asSMessageInfo;

namespace CoreEngine::Script
{
    /// @brief コンパイラのメッセージを Script カテゴリのログへ出す（`SetMessageCallback` に渡す）
    /// @details `セクション(行, 列) : 本文` の形で、エラー・警告・情報をそれぞれのログレベルで出す。
    /// @brief コンパイラのメッセージをログへ流す
    /// @param userData `std::set<std::string>*` を渡すと、エラーの出たファイルを集める
    ///        （壊れたファイルだけ前の版へ差し戻すのに使う）。不要なら nullptr
    void OnCompilerMessage(const asSMessageInfo* message, void* userData);

    /// @brief 止まった実行の場所と呼び出し履歴をエラーとしてログへ出す
    /// @param context 止まったコンテキスト（Unprepare する前のもの）
    /// @param headline 1 行目に出す見出し
    void LogFailedExecution(asIScriptContext* context, const std::string& headline);
}
