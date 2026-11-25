#pragma once

#include <filesystem>

namespace CoreEngine
{
    /// @brief 1 つのフォルダの中のファイルが足された・消えた・書き換わったかを、待たずに確かめる
    /// @details 下のフォルダは見ない。前に確かめた後の変更は、何回あっても 1 回の「変わった」にまとめる。
    ///          見張っている間は、そのフォルダを含む親のフォルダの名前を変えたり動かしたりできない。
    class FolderChangeWatch
    {
    public:
        FolderChangeWatch() = default;
        ~FolderChangeWatch();

        FolderChangeWatch(const FolderChangeWatch&) = delete;
        FolderChangeWatch& operator=(const FolderChangeWatch&) = delete;

        /// @brief 見張り始める（前の見張りは終える）
        /// @return 始められたら true（フォルダが無い・見張れないときは false）
        bool Start(const std::filesystem::path& folder);

        /// @brief 見張りを終える
        void Stop();

        /// @brief 見張っているか
        bool IsWatching() const { return handle_ != nullptr; }

        /// @brief 見張っているフォルダ（見張っていなければ空）
        const std::filesystem::path& GetFolder() const { return folder_; }

        /// @brief 前に呼んだ後（初めてなら見張りを始めた後）に、フォルダの中が変わったか
        /// @return 変わったら true。見張りを続けられなくなったときも true を返し、見張りを終える
        bool TakeChanged();

    private:
        void* handle_ = nullptr; ///< 変更の知らせのハンドル
        std::filesystem::path folder_;
    };
}
