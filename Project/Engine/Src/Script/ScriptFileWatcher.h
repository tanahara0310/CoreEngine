#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace CoreEngine
{
    /// @brief スクリプトのフォルダの `.as` の変更を別のスレッドで見張り、メインスレッドへ渡す
    /// @details 変更は溜めておき、最後の変更から一定の時間が過ぎてから渡す
    ///          （エディタは 1 回の保存で複数の変更を出すため）。
    class ScriptFileWatcher
    {
    public:
        ScriptFileWatcher() = default;
        ~ScriptFileWatcher();

        ScriptFileWatcher(const ScriptFileWatcher&) = delete;
        ScriptFileWatcher& operator=(const ScriptFileWatcher&) = delete;

        /// @brief 見張り始める（下のフォルダも見る）
        /// @return 始められたら true（フォルダが無い・開けないときは false）
        bool Start(const std::filesystem::path& root);

        /// @brief 見張りを終える
        void Stop();

        /// @brief 見張っているか
        bool IsWatching() const { return handle_ != nullptr; }

        /// @brief 最後の変更から settleTime が過ぎていれば、溜まった変更を受け取る
        /// @param changedFiles 変更のあった `.as` のパス（同じものは 1 つだけ。取りこぼしたときはフォルダのパス）
        /// @return 受け取るものがあれば true
        bool TakeSettledChanges(std::chrono::milliseconds settleTime, std::vector<std::filesystem::path>& changedFiles);

    private:
        /// @brief 見張りのスレッドの本体
        void Run();

        /// @brief 変更を溜める
        void Record(std::filesystem::path path);

        void* handle_ = nullptr;    ///< 見張るフォルダのハンドル
        void* stopEvent_ = nullptr; ///< 終わりを知らせるイベント
        std::filesystem::path root_;
        std::thread thread_;

        std::mutex mutex_;
        std::vector<std::filesystem::path> changed_;
        std::chrono::steady_clock::time_point lastChange_{};
    };
}
