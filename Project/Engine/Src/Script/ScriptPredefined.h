#pragma once

#include <filesystem>
#include <string>

class asIScriptEngine;

namespace CoreEngine::Script
{
    /// @brief 登録済みの列挙・funcdef・型・関数・グローバル変数・typedef を、AngelScript の宣言の形で並べる
    /// @details `as.predefined`（AngelScript Language Server が読む宣言のファイル）の中身になる。
    ///          並びは登録順で、同じ名前空間のものは 1 つの `namespace` にまとめる。改行は CRLF。
    ///          テンプレートに型を当てはめた型と、アプリの登録でしか書けない形（`?&` の型・`&out` の `= void`・`...`）を含む宣言は書かない。
    std::string BuildScriptPredefined(asIScriptEngine& engine);

    /// @brief `BuildScriptPredefined` の結果をファイルへ書く（中身が同じなら書かない）
    /// @return 書き終えたか、既に同じ中身だったら true
    bool WriteScriptPredefined(asIScriptEngine& engine, const std::filesystem::path& file);
}
