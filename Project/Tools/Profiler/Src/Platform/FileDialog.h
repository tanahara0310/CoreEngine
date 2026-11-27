#pragma once

#include <Windows.h>

#include <optional>
#include <string>

/// @file FileDialog.h
/// @brief Windows のファイルを選ぶ窓（.tracy を開く・保存する）

namespace CoreEngine::Profiler::Platform
{
    /// @brief .tracy を選んで開く窓を出す
    /// @return 選んだファイルのパス。やめたら空
    std::optional<std::wstring> ShowOpenTraceDialog(HWND owner);

    /// @brief .tracy の保存先を選ぶ窓を出す（同じ名前があれば上書きを確かめる）
    /// @param defaultName 最初に入っているファイル名
    /// @return 選んだファイルのパス。やめたら空
    std::optional<std::wstring> ShowSaveTraceDialog(HWND owner, const std::wstring& defaultName);
}
