#include "pch.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <array>
#include <vector>
#include <windows.h>
#include <shellapi.h>

namespace CoreEngine
{
    namespace
    {
        /// @brief ソースツリーを見分ける目印
        constexpr const char* kMarker = "CoreEngine.vcxproj";

        /// @brief 作り直せるものを入れるフォルダ名
        constexpr const char* kIntermediateDir = "Intermediate";

        /// @brief プロジェクトの根を指定する起動の引数
        constexpr std::wstring_view kProjectOption = L"--project";

        /// @brief プロジェクトの根から辿る綴りの先頭
        constexpr const wchar_t* kProjectTop = L"Application";

        /// @brief エンジンの根から辿るアセットの綴りの先頭
        constexpr const wchar_t* kEngineTop = L"Engine";

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

        /// @brief path を UTF-8 の文字列にする
        std::string ToUtf8(const std::filesystem::path& path)
        {
            const std::wstring& wide = path.native();
            if (wide.empty()) {
                return {};
            }
            const int length = WideCharToMultiByte(
                CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
            if (length <= 0) {
                return {};
            }
            std::string utf8(static_cast<size_t>(length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                utf8.data(), length, nullptr, nullptr);
            return utf8;
        }

        /// @brief パスの 1 要素が指定の名前か（大文字小文字は区別しない）
        bool IsNamed(const std::filesystem::path& element, const wchar_t* name)
        {
            return ::_wcsicmp(element.c_str(), name) == 0;
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

        /// @brief 起動の引数から `--project` の値を取り出す
        /// @return `--project <フォルダ>` か `--project=<フォルダ>` の値。無ければ空
        std::filesystem::path ProjectOptionFromCommandLine()
        {
            int count = 0;
            LPWSTR* const args = ::CommandLineToArgvW(::GetCommandLineW(), &count);
            if (!args) {
                return {};
            }

            std::filesystem::path value;
            for (int i = 1; i < count; ++i) {
                const std::wstring_view arg = args[i];
                if (arg == kProjectOption) {
                    if (i + 1 < count) {
                        value = std::filesystem::path(args[i + 1]);
                    }
                    break;
                }
                if (arg.size() > kProjectOption.size() && arg.starts_with(kProjectOption) &&
                    arg[kProjectOption.size()] == L'=') {
                    value = std::filesystem::path(std::wstring(arg.substr(kProjectOption.size() + 1)));
                    break;
                }
            }
            ::LocalFree(args);
            return value;
        }

        /// @brief 末尾の区切りを落とした絶対パスにする
        std::filesystem::path ToAbsoluteFolder(const std::filesystem::path& path)
        {
            std::error_code ec;
            std::filesystem::path absolute = std::filesystem::absolute(path, ec);
            if (ec) {
                return {};
            }
            absolute = absolute.lexically_normal();
            if (!absolute.has_filename() && absolute.has_relative_path()) {
                absolute = absolute.parent_path();
            }
            return absolute;
        }

        /// @brief 同梱プロジェクトのフォルダ名
        constexpr const char* kBundledProjectsDir = "Projects";

        /// @brief フォルダの直下にあるプロジェクトのうち、名前が最初のもの（無ければ空）
        std::filesystem::path FirstProjectIn(const std::filesystem::path& directory)
        {
            std::error_code ec;
            std::vector<std::filesystem::path> projects;
            for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
                if (entry.is_directory(ec) && ProjectPaths::IsProjectFolder(entry.path())) {
                    projects.push_back(entry.path());
                }
            }
            if (projects.empty()) {
                return {};
            }
            std::sort(projects.begin(), projects.end());
            return projects.front();
        }

        /// @brief `--project` を使わないときのプロジェクトの根
        /// @param note どう決まったか（起動ログ用）を受け取る
        std::filesystem::path DefaultProjectRoot(const std::filesystem::path& engineRoot, std::string& note)
        {
            if (ProjectPaths::IsProjectFolder(engineRoot)) {
                note = "エンジンと同じ根";
                return engineRoot;
            }
#ifdef CORE_EDITOR
            if (std::filesystem::path first = FirstProjectIn(engineRoot.parent_path() / kBundledProjectsDir);
                !first.empty()) {
                note = "Projects の中で名前が最初のもの";
                return first;
            }
#endif
            note = "エンジンと同じ根（プロジェクトが見つからない）";
            return engineRoot;
        }

        struct Resolved
        {
            std::filesystem::path engineRoot;
            std::filesystem::path projectRoot;
            std::string note;
            bool projectSpecified = false;
        };

        Resolved DetermineRoots()
        {
            Resolved resolved;
            const std::filesystem::path exeDir = ExecutableDirectory();
            resolved.engineRoot = exeDir;
            resolved.note = "エンジンは exe の隣";

#ifdef CORE_EDITOR
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
                    resolved.engineRoot = found;
                    resolved.note = "エンジンはソースツリー";
                    break;
                }
            }
#endif

            const std::filesystem::path option = ProjectOptionFromCommandLine();
            if (!option.empty()) {
                const std::filesystem::path folder = ToAbsoluteFolder(option);
                if (!folder.empty() && ProjectPaths::IsProjectFolder(folder)) {
                    resolved.projectRoot = folder;
                    resolved.projectSpecified = true;
                    resolved.note += "・プロジェクトは --project で指定";
                    return resolved;
                }
                resolved.note += "・--project の " + ToUtf8(option) + " はプロジェクトのフォルダではない";
            }

            std::string defaultNote;
            resolved.projectRoot = DefaultProjectRoot(resolved.engineRoot, defaultNote);
            resolved.note += "・プロジェクトは" + defaultNote;
            return resolved;
        }

        const Resolved& Store()
        {
            static const Resolved resolved = DetermineRoots();
            return resolved;
        }
    }

    void ProjectPaths::Prime()
    {
        (void)Store();
    }

    const std::filesystem::path& ProjectPaths::EngineRoot()
    {
        return Store().engineRoot;
    }

    const std::filesystem::path& ProjectPaths::ProjectRoot()
    {
        return Store().projectRoot;
    }

    bool ProjectPaths::IsProjectSpecified()
    {
        return Store().projectSpecified;
    }

    const std::string& ProjectPaths::ResolutionNote()
    {
        return Store().note;
    }

    std::filesystem::path ProjectPaths::Resolve(std::string_view relative)
    {
        std::filesystem::path path = FromUtf8(relative);
        if (path.empty()) {
            return ProjectRoot();
        }
        if (path.is_absolute()) {
            return path;
        }
        path = path.lexically_normal();
        const bool inProject = !path.empty() && IsNamed(*path.begin(), kProjectTop);
        return (inProject ? ProjectRoot() : EngineRoot()) / path;
    }

    std::filesystem::path ProjectPaths::MakeRelative(const std::filesystem::path& absolute)
    {
        const std::filesystem::path normalized = absolute.lexically_normal();
        const auto relativeUnder = [&normalized](const std::filesystem::path& root, const wchar_t* top) {
            std::filesystem::path relative = normalized.lexically_relative(root);
            if (relative.empty() || !IsNamed(*relative.begin(), top)) {
                relative.clear();
            }
            return relative;
        };

        if (std::filesystem::path relative = relativeUnder(ProjectRoot(), kProjectTop); !relative.empty()) {
            return relative;
        }
        return relativeUnder(EngineRoot(), kEngineTop);
    }

    std::filesystem::path ProjectPaths::Intermediate(std::string_view relative)
    {
        std::filesystem::path path = ProjectRoot() / kIntermediateDir;
        if (!relative.empty()) {
            path /= FromUtf8(relative);
        }
        return path;
    }

    std::filesystem::path ProjectPaths::EngineIntermediate(std::string_view relative)
    {
        std::filesystem::path path = EngineRoot() / kIntermediateDir;
        if (!relative.empty()) {
            path /= FromUtf8(relative);
        }
        return path;
    }

    bool ProjectPaths::IsProjectFolder(const std::filesystem::path& folder)
    {
        std::error_code ec;
        return std::filesystem::is_regular_file(
            folder / "Application" / "Config" / "EngineSettings" / "Project.json", ec);
    }

    std::filesystem::path ProjectPaths::BundledProjectsDirectory()
    {
        return EngineRoot().parent_path() / kBundledProjectsDir;
    }
}
