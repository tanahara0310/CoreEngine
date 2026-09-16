#pragma once

#include <filesystem>

namespace CoreEngine::Editor
{
    /// @brief 外部のコードエディタ（VS Code）でファイルの行を開く
    /// @param file 開くファイル（絶対パス）
    /// @param line 1 始まりの行（0 以下なら行を指定しない）
    /// @param column 1 始まりの桁（0 以下なら桁を指定しない）
    /// @return 起動を頼めたら true
    bool OpenInCodeEditor(const std::filesystem::path& file, int line = 0, int column = 0);
}
