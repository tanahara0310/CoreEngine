#include "pch.h"
#include "Utility/Path/ProjectPaths.h"

#include <array>
#include <windows.h>

namespace CoreEngine
{
    namespace
    {
        /// @brief ソースツリーを見分ける目印
        constexpr const char* kMarker = "CoreEngine.vcxproj";

        /// @brief 作り直せるものを入れるフォルダ名
        constexpr const char* kIntermediateDir = "Intermediate";

        /// @brief 上へ辿る段数の上限（辿り切らずに止めるための歯止め）
        constexpr int kMaxAscend = 8;

        /// @brief UTF-8 の綴りを path にする
        /// @note `std::filesystem::path(std::string)` は ANSI として解釈するので使わない。
        ///       日本語を含むパスで例外になる。
        std::filesystem::path FromUtf8(std::string_view utf8)
        {
            if (utf8.empty()) {
                return {};
            }
            const int length = MultiByteToWideChar(
                CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            if (length <= 0) {
                return {};
            }
            std::wstring wide(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                wide.data(), length);
            return std::filesystem::path(wide);
        }

        std::filesystem::path ExecutableDirectory()
        {
            std::array<wchar_t, MAX_PATH * 4> buffer{};
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                static_cast<DWORD>(buffer.size()));
            if (length == 0) {
                return {};
            }
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        }

        /// @brief 候補から上へ辿って目印を探す
        /// @return 見つかった `Project/`。見つからなければ空
        std::filesystem::path FindSourceRoot(const std::filesystem::path& start)
        {
            std::error_code ec;
            std::filesystem::path current = start;
            for (int i = 0; i < kMaxAscend && !current.empty(); ++i) {
                // 候補そのものが Project/ の場合
                if (std::filesystem::exists(current / kMarker, ec)) {
                    return current;
                }
                // 候補がリポジトリの根の場合
                if (std::filesystem::exists(current / "Project" / kMarker, ec)) {
                    return current / "Project";
                }
                const std::filesystem::path parent = current.parent_path();
                if (parent == current) {
                    break;
                }
                current = parent;
            }
            return {};
        }

        struct Resolved
        {
            std::filesystem::path root;
            std::string note;
        };

        Resolved DetermineRoot()
        {
            const std::filesystem::path exeDir = ExecutableDirectory();

#ifdef USE_IMGUI
            // エディタを持つビルドはソースツリーへ書く。調整した値がそのまま
            // リポジトリに乗り、起動方法によって保存先が分かれなくなる。
            // エディタの無いビルド（Release）は配布形態と同じく exe の隣を見る
            std::error_code ec;
            const std::filesystem::path candidates[] = {
                std::filesystem::current_path(ec),
                exeDir,
            };
            for (const auto& candidate : candidates) {
                if (candidate.empty()) {
                    continue;
                }
                if (std::filesystem::path found = FindSourceRoot(candidate); !found.empty()) {
                    return { found, "ソースツリー" };
                }
            }
#endif

            return { exeDir, "exe の隣" };
        }

        Resolved& Store()
        {
            static Resolved resolved = DetermineRoot();
            return resolved;
        }
    }

    void ProjectPaths::Prime()
    {
        (void)Store();
    }

    const std::filesystem::path& ProjectPaths::Root()
    {
        return Store().root;
    }

    const std::string& ProjectPaths::ResolutionNote()
    {
        return Store().note;
    }

    std::filesystem::path ProjectPaths::Resolve(std::string_view relative)
    {
        std::filesystem::path path = FromUtf8(relative);
        if (path.empty()) {
            return Root();
        }
        if (path.is_absolute()) {
            return path;
        }
        return Root() / path;
    }

    std::filesystem::path ProjectPaths::Intermediate(std::string_view relative)
    {
        std::filesystem::path path = Root() / kIntermediateDir;
        if (!relative.empty()) {
            path /= FromUtf8(relative);
        }
        return path;
    }
}
