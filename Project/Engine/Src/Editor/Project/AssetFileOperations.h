#pragma once

#ifdef CORE_EDITOR

#include <filesystem>
#include <string>

namespace CoreEngine::Editor
{
    /// @brief Project ビューからのファイル操作（実物のフォルダを動かす）
    /// @details エクスプローラを開かずに、名前の変更・コピー・移動・削除ができるようにする。
    /// @note アセットの `.meta` は GUID を持っているので扱いを分ける。
    ///       名前の変更と移動は `.meta` も連れていく（GUID が変わらないので参照が切れない）。
    ///       コピーは `.meta` を持っていかない（同じ GUID が 2 つになるため。複製には新しい GUID が付く）。
    ///       削除はごみ箱へ送るので、間違えても戻せる。
    namespace AssetFileOperations
    {
        /// @brief 名前を変える（同じフォルダの中で移す）
        /// @param target 変える対象（ファイルでもフォルダでもよい）
        /// @param newName 新しい名前（拡張子を含む。パス区切りは入れられない）
        /// @param outPath 変えた後のパス（省略可）
        /// @param outError できなかったときの訳（省略可）
        bool Rename(const std::filesystem::path& target, const std::string& newName,
                    std::filesystem::path* outPath = nullptr, std::string* outError = nullptr);

        /// @brief 別のフォルダへ複製する（同じフォルダなら名前の後ろに番号が付く）
        /// @param source 複製する元（ファイルでもフォルダでもよい）
        /// @param destinationFolder 複製先のフォルダ
        bool Copy(const std::filesystem::path& source, const std::filesystem::path& destinationFolder,
                  std::filesystem::path* outPath = nullptr, std::string* outError = nullptr);

        /// @brief 別のフォルダへ移す
        /// @param source 移す元（ファイルでもフォルダでもよい）
        /// @param destinationFolder 移す先のフォルダ
        bool Move(const std::filesystem::path& source, const std::filesystem::path& destinationFolder,
                  std::filesystem::path* outPath = nullptr, std::string* outError = nullptr);

        /// @brief ごみ箱へ送る
        /// @param target 消す対象（ファイルでもフォルダでもよい）
        /// @note 完全に消すのではなくごみ箱へ入れるので、間違えたらエクスプローラから戻せる。
        bool MoveToRecycleBin(const std::filesystem::path& target, std::string* outError = nullptr);

        /// @brief フォルダを作る
        /// @param parentFolder 作る場所
        /// @param name フォルダの名前
        bool CreateFolder(const std::filesystem::path& parentFolder, const std::string& name,
                          std::filesystem::path* outPath = nullptr, std::string* outError = nullptr);

        /// @brief 名前として使えるか（パス区切り・Windows が禁じる文字・予約名を弾く）
        bool IsValidName(const std::string& name, std::string* outError = nullptr);

        /// @brief 触ってよい場所か（プロジェクトの Assets の中だけを許す）
        bool IsEditableLocation(const std::filesystem::path& target, std::string* outError = nullptr);

        /// @brief 同じ名前があれば「名前 (2)」のように空いている名前を作る
        std::filesystem::path MakeUniquePath(const std::filesystem::path& desired);
    }
}

#endif // CORE_EDITOR
