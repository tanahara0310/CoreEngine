#pragma once

#include <filesystem>
#include <string>

namespace CoreEngine
{
    /// @brief この exe を起動し直す
    /// @details 頼んだ時点では終わりを始めるだけで、新しいプロセスは終わりの処理が済んでから起動する。
    namespace Relaunch
    {
        /// @brief 終わったあとに arguments を付けて起動し直すよう頼み、終わりを始める
        void Request(std::wstring arguments);

        /// @brief 終わったあとに folder のプロジェクトを開くよう頼み、終わりを始める
        void RequestProject(const std::filesystem::path& folder);

        /// @brief 頼まれていれば起動し直す（終わりの処理が済んでから呼ぶ）
        void RunIfRequested();
    }
}
