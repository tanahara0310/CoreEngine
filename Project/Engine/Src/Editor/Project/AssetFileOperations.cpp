#include "pch.h"
#include "Editor/Project/AssetFileOperations.h"

#ifdef CORE_EDITOR

#include "Graphics/Asset/AssetDatabase.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <array>
#include <string_view>

#include <Windows.h>
#include <shellapi.h>

namespace CoreEngine::Editor::AssetFileOperations
{
    namespace
    {
        /// @brief 触ってよい場所（プロジェクトの根からの相対パス）
        constexpr std::array<std::string_view, 2> kEditableRoots = {
            "Application/Assets",
            "Engine/Assets",
        };

        /// @brief Windows がファイル名に使えない文字
        constexpr std::string_view kForbiddenChars = "\\/:*?\"<>|";

        /// @brief Windows が予約している名前（拡張子を除いた部分で照合する）
        constexpr std::array<std::string_view, 22> kReservedNames = {
            "CON", "PRN", "AUX", "NUL",
            "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
        };

        /// @brief `.meta` のパス（アセット本体の隣。AssetMetadata と同じ決まり）
        std::filesystem::path MetaPathOf(const std::filesystem::path& assetPath)
        {
            std::filesystem::path meta = assetPath;
            meta += ".meta";
            return meta;
        }

        /// @brief 大文字にする（予約名の照合用）
        std::string ToUpper(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return text;
        }

        /// @brief ごみ箱へ送る（SHFileOperation は終端が 2 個の null の文字列を要求する）
        bool ShellRecycle(const std::filesystem::path& target)
        {
            std::wstring from = target.wstring();
            from.push_back(L'\0');
            from.push_back(L'\0');

            SHFILEOPSTRUCTW operation{};
            operation.wFunc = FO_DELETE;
            operation.pFrom = from.c_str();
            // ALLOWUNDO でごみ箱へ入る。確認はエディタ側で出すので NOCONFIRMATION
            operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
            return SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted;
        }

        /// @brief 変えた後にアセットの登録を作り直す
        void RefreshDatabase()
        {
            AssetDatabase::GetInstance().Refresh();
        }
    }

    bool IsValidName(const std::string& name, std::string* outError)
    {
        const auto fail = [outError](const char* reason) {
            if (outError) { *outError = reason; }
            return false;
            };

        if (name.empty()) {
            return fail("名前を入れてください");
        }
        if (name == "." || name == "..") {
            return fail("その名前は使えません");
        }
        if (name.find_first_of(kForbiddenChars) != std::string::npos) {
            return fail("\\ / : * ? \" < > | は使えません");
        }
        if (name.back() == ' ' || name.back() == '.') {
            return fail("最後を空白や . にはできません");
        }
        const std::string stem = ToUpper(name.substr(0, name.find('.')));
        if (std::find(kReservedNames.begin(), kReservedNames.end(), stem) != kReservedNames.end()) {
            return fail("Windows が予約している名前です");
        }
        if (outError) { outError->clear(); }
        return true;
    }

    bool IsEditableLocation(const std::filesystem::path& target, std::string* outError)
    {
        const auto fail = [outError](const char* reason) {
            if (outError) { *outError = reason; }
            return false;
            };

        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::weakly_canonical(target, ec);
        const std::filesystem::path base = ec ? target.lexically_normal() : absolute;

        for (const std::string_view root : kEditableRoots) {
            const std::filesystem::path rootPath =
                std::filesystem::weakly_canonical(ProjectPaths::Resolve(root), ec);
            if (ec) { continue; }
            const std::filesystem::path relative = base.lexically_relative(rootPath);
            if (!relative.empty() && *relative.begin() != "..") {
                if (outError) { outError->clear(); }
                return true;
            }
        }
        return fail("Assets の中のものだけ操作できます");
    }

    std::filesystem::path MakeUniquePath(const std::filesystem::path& desired)
    {
        std::error_code ec;
        if (!std::filesystem::exists(desired, ec)) {
            return desired;
        }

        const std::filesystem::path parent = desired.parent_path();
        const std::filesystem::path extension = desired.extension();
        // フォルダには拡張子の概念が無いので、ファイルのときだけ茎と拡張子に分ける
        const bool isDirectory = std::filesystem::is_directory(desired, ec);
        const std::string stem = isDirectory ? desired.filename().string() : desired.stem().string();

        for (int index = 2; index < 1000; ++index) {
            std::filesystem::path candidate = parent / (stem + " (" + std::to_string(index) + ")");
            if (!isDirectory) {
                candidate += extension;
            }
            if (!std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
        }
        return desired;
    }

    bool Rename(const std::filesystem::path& target, const std::string& newName,
                std::filesystem::path* outPath, std::string* outError)
    {
        const auto fail = [outError](const std::string& reason) {
            if (outError) { *outError = reason; }
            return false;
            };

        if (!IsValidName(newName, outError) || !IsEditableLocation(target, outError)) {
            return false;
        }

        std::error_code ec;
        if (!std::filesystem::exists(target, ec)) {
            return fail("元のファイルが見つかりません");
        }

        const std::filesystem::path destination = target.parent_path() / newName;
        if (destination == target) {
            if (outPath) { *outPath = target; }
            return true;
        }
        if (std::filesystem::exists(destination, ec)) {
            return fail("同じ名前のものがもうあります");
        }

        std::filesystem::rename(target, destination, ec);
        if (ec) {
            return fail("名前を変えられませんでした（開いているかもしれません）");
        }

        // GUID を持つ .meta も一緒に連れていく（置いていくと参照が切れる）
        const std::filesystem::path meta = MetaPathOf(target);
        if (std::filesystem::exists(meta, ec)) {
            std::error_code metaEc;
            std::filesystem::rename(meta, MetaPathOf(destination), metaEc);
        }

        RefreshDatabase();
        if (outPath) { *outPath = destination; }
        if (outError) { outError->clear(); }
        Logger& logger = Logger::GetInstance();
        logger.Logf(LogLevel::Info, LogCategory::System, "名前を変えました: {} → {}",
            logger.PathToUtf8(target.filename()), newName);
        return true;
    }

    bool Copy(const std::filesystem::path& source, const std::filesystem::path& destinationFolder,
              std::filesystem::path* outPath, std::string* outError)
    {
        const auto fail = [outError](const std::string& reason) {
            if (outError) { *outError = reason; }
            return false;
            };

        if (!IsEditableLocation(source, outError) || !IsEditableLocation(destinationFolder, outError)) {
            return false;
        }

        std::error_code ec;
        if (!std::filesystem::exists(source, ec)) {
            return fail("元のファイルが見つかりません");
        }
        if (!std::filesystem::is_directory(destinationFolder, ec)) {
            return fail("複製先がフォルダではありません");
        }

        const bool isDirectory = std::filesystem::is_directory(source, ec);
        if (isDirectory) {
            const std::filesystem::path normalizedSource = source.lexically_normal();
            const std::filesystem::path relative =
                destinationFolder.lexically_normal().lexically_relative(normalizedSource);
            if (!relative.empty() && *relative.begin() != "..") {
                return fail("自分の中へは複製できません");
            }
        }

        const std::filesystem::path destination =
            MakeUniquePath(destinationFolder / source.filename());

        if (isDirectory) {
            std::filesystem::copy(source, destination,
                std::filesystem::copy_options::recursive, ec);
        } else {
            std::filesystem::copy_file(source, destination, ec);
        }
        if (ec) {
            return fail("複製できませんでした");
        }

        // .meta は連れていかない。複製には新しい GUID を付けたいので、
        // フォルダごと複製したときは中の .meta も消しておく
        std::error_code metaEc;
        if (isDirectory) {
            for (auto it = std::filesystem::recursive_directory_iterator(destination, metaEc);
                 !metaEc && it != std::filesystem::recursive_directory_iterator(); it.increment(metaEc)) {
                if (it->is_regular_file(metaEc) && it->path().extension() == ".meta") {
                    std::error_code removeEc;
                    std::filesystem::remove(it->path(), removeEc);
                }
            }
        } else {
            std::filesystem::remove(MetaPathOf(destination), metaEc);
        }

        RefreshDatabase();
        if (outPath) { *outPath = destination; }
        if (outError) { outError->clear(); }
        Logger& logger = Logger::GetInstance();
        logger.Logf(LogLevel::Info, LogCategory::System, "複製しました: {} → {}",
            logger.PathToUtf8(source.filename()), logger.PathToUtf8(destination));
        return true;
    }

    bool Move(const std::filesystem::path& source, const std::filesystem::path& destinationFolder,
              std::filesystem::path* outPath, std::string* outError)
    {
        const auto fail = [outError](const std::string& reason) {
            if (outError) { *outError = reason; }
            return false;
            };

        if (!IsEditableLocation(source, outError) || !IsEditableLocation(destinationFolder, outError)) {
            return false;
        }

        std::error_code ec;
        if (!std::filesystem::exists(source, ec)) {
            return fail("元のファイルが見つかりません");
        }
        if (!std::filesystem::is_directory(destinationFolder, ec)) {
            return fail("移す先がフォルダではありません");
        }
        if (source.parent_path().lexically_normal() == destinationFolder.lexically_normal()) {
            if (outPath) { *outPath = source; }
            return true;  // 同じ場所なので何もしない
        }
        if (std::filesystem::is_directory(source, ec)) {
            const std::filesystem::path relative =
                destinationFolder.lexically_normal().lexically_relative(source.lexically_normal());
            if (!relative.empty() && *relative.begin() != "..") {
                return fail("自分の中へは移せません");
            }
        }

        const std::filesystem::path destination = destinationFolder / source.filename();
        if (std::filesystem::exists(destination, ec)) {
            return fail("移す先に同じ名前のものがあります");
        }

        std::filesystem::rename(source, destination, ec);
        if (ec) {
            return fail("移せませんでした（開いているかもしれません）");
        }

        // GUID を保つため .meta も一緒に移す
        const std::filesystem::path meta = MetaPathOf(source);
        if (std::filesystem::exists(meta, ec)) {
            std::error_code metaEc;
            std::filesystem::rename(meta, MetaPathOf(destination), metaEc);
        }

        RefreshDatabase();
        if (outPath) { *outPath = destination; }
        if (outError) { outError->clear(); }
        Logger& logger = Logger::GetInstance();
        logger.Logf(LogLevel::Info, LogCategory::System, "移しました: {} → {}",
            logger.PathToUtf8(source.filename()), logger.PathToUtf8(destinationFolder));
        return true;
    }

    bool MoveToRecycleBin(const std::filesystem::path& target, std::string* outError)
    {
        const auto fail = [outError](const std::string& reason) {
            if (outError) { *outError = reason; }
            return false;
            };

        if (!IsEditableLocation(target, outError)) {
            return false;
        }

        std::error_code ec;
        if (!std::filesystem::exists(target, ec)) {
            return fail("消すものが見つかりません");
        }

        // 本体の前に .meta を送る（本体だけ消えて .meta が残る状態を作らない）
        const std::filesystem::path meta = MetaPathOf(target);
        if (std::filesystem::exists(meta, ec)) {
            ShellRecycle(meta);
        }
        if (!ShellRecycle(target)) {
            return fail("ごみ箱へ送れませんでした（開いているかもしれません）");
        }

        RefreshDatabase();
        if (outError) { outError->clear(); }
        Logger& logger = Logger::GetInstance();
        logger.Logf(LogLevel::Info, LogCategory::System, "ごみ箱へ送りました: {}",
            logger.PathToUtf8(target));
        return true;
    }
}

#endif // CORE_EDITOR
