#pragma once

#include <Windows.h>

#include <filesystem>
#include <future>

/// @file ProfilerLauncher.h
/// @brief エディタの exe の隣にある CoreEngine Profiler（CoreEngineProfiler.exe）を起動する

namespace CoreEngine::Editor
{
    /// @brief CoreEngine Profiler の exe（エディタの exe と同じフォルダ）
    std::filesystem::path ProfilerExecutablePath();

    /// @brief CoreEngine Profiler を起動して、このエディタ（127.0.0.1:8086）へつながせる
    /// @details もう開いていれば、そのプロファイラーが前へ出る（記録が無ければつなぐ）
    /// @return 起動を頼めたら true
    bool OpenProfiler();

    /// @brief CoreEngine Profiler で .tracy を開く
    /// @return 起動を頼めたら true
    bool OpenTraceInProfiler(const std::filesystem::path& trace);

    /// @brief .tracy を選ぶ窓を裏のスレッドで出し、選んだら CoreEngine Profiler で開く
    class TraceFilePicker
    {
    public:
        /// @brief 選ぶ窓を出す（もう出ていれば何もしない）
        void Open(HWND owner);

        /// @brief 毎フレーム呼ぶ。選び終わっていたらプロファイラーで開く
        void Update();

        /// @brief 選ぶ窓が出ているか
        bool IsOpen() const { return picking_.valid(); }

    private:
        std::future<std::filesystem::path> picking_;
    };
}
