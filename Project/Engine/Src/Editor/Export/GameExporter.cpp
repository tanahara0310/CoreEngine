#include "pch.h"
#include "Editor/Export/GameExporter.h"

#ifdef CORE_EDITOR

#include "Editor/Export/ShaderCooker.h"
#include "Editor/Export/TextureCooker.h"
#include "EngineSystem/Startup/SplashScreen.h"
#include "Graphics/Texture/Cook/CookedTexture.h"
#include "Utility/Path/ProjectPaths.h"

#include <Windows.h>
#include <objbase.h>

#include <chrono>
#include <format>
#include <vector>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief 写すファイル 1 つ
        struct CopyItem
        {
            std::filesystem::path from;
            std::filesystem::path to;
        };

        /// @brief 丸ごと写すフォルダ 1 つ
        struct FolderItem
        {
            std::filesystem::path from;
            std::filesystem::path to;
        };

        /// @brief path を表示用の UTF-8 にする
        std::string ToUtf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        /// @brief 動いている exe のパス
        std::filesystem::path RunningExe()
        {
            wchar_t path[MAX_PATH] = {};
            ::GetModuleFileNameW(nullptr, path, MAX_PATH);
            return std::filesystem::path(path);
        }

        /// @brief ファイルを最後に書いた日時を "2026-09-26 13:25" にする（取れなければ空）
        std::string FormatWriteTime(const std::filesystem::path& path)
        {
            std::error_code ec;
            const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, ec);
            if (ec) {
                return {};
            }
            using namespace std::chrono;
            const zoned_time local{ current_zone(), floor<minutes>(clock_cast<system_clock>(written)) };
            return std::format("{:%Y-%m-%d %H:%M}", local);
        }

        /// 消せない・書けないときに添える一言
        constexpr const char* kLockedHint = "（書き出したゲームが動いていないか確かめてください）";
    }

    ExportPlan GameExporter::Plan()
    {
        ExportPlan plan;
        plan.destination = ProjectPaths::ProjectRoot() / "Build" / "Windows";

        // Release の exe は、動いている exe の出力先と並んだ Release のフォルダにある
        const std::filesystem::path running = RunningExe();
        plan.releaseExe = running.parent_path().parent_path() / "Release" / running.filename();
        std::error_code ec;
        if (!std::filesystem::is_regular_file(plan.releaseExe, ec)) {
            plan.error = "先に Release でビルドしてください（" + ToUtf8(plan.releaseExe) + " がありません）";
            return plan;
        }
        plan.releaseBuiltAt = FormatWriteTime(plan.releaseExe);
        return plan;
    }

    ExportResult GameExporter::Export(const ExportPlan& plan, ExportProgress& progress)
    {
        ExportResult result;
        if (!plan.error.empty()) {
            result.error = plan.error;
            return result;
        }

        const std::filesystem::path& destination = plan.destination;
        const FolderItem folders[] = {
            { ProjectPaths::EngineRoot() / "Engine" / "Assets", destination / "Engine" / "Assets" },
            { ProjectPaths::ProjectRoot() / "Application" / "Assets", destination / "Application" / "Assets" },
            { ProjectPaths::ProjectRoot() / "Application" / "Config", destination / "Application" / "Config" },
        };
        const std::filesystem::path cookedFolders[] = {
            destination / "Engine" / "Cooked",
            destination / "Application" / "Cooked",
        };

        // 写すファイルを先に集める（Release のフォルダの exe と DLL、各フォルダの中身）。
        // 画像のテクスチャは写さずに DDS へ変換する
        std::vector<CopyItem> items;
        std::vector<std::filesystem::path> textures;
        std::error_code ec;
        const std::filesystem::path releaseDirectory = plan.releaseExe.parent_path();
        for (const auto& entry : std::filesystem::directory_iterator(releaseDirectory, ec)) {
            const std::filesystem::path extension = entry.path().extension();
            if (entry.is_regular_file(ec) && (extension == ".exe" || extension == ".dll")) {
                items.push_back({ entry.path(), destination / entry.path().filename() });
            }
        }
        if (ec) {
            result.error = ToUtf8(releaseDirectory) + " を読めませんでした";
            return result;
        }
        for (const FolderItem& folder : folders) {
            std::filesystem::recursive_directory_iterator it(folder.from, ec);
            for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (!it->is_regular_file(ec)) {
                    continue;
                }
                if (CookedTexture::IsCookable(it->path())) {
                    textures.push_back(it->path());
                } else {
                    items.push_back({ it->path(), folder.to / it->path().lexically_relative(folder.from) });
                }
            }
            if (ec) {
                result.error = ToUtf8(folder.from) + " を読めませんでした";
                return result;
            }
        }
        // ローディング画面の画像は D3D12 より前に WIC で読むので、DDS にした物とは別に元のまま写す
        if (const std::filesystem::path splashImage = SplashScreen::ResolveImagePath(); !splashImage.empty()) {
            if (const std::filesystem::path relative = ProjectPaths::MakeRelative(splashImage); !relative.empty()) {
                items.push_back({ splashImage, destination / relative });
            }
        }
        const std::vector<CookTarget> targets = TextureCooker::PlanTargets(textures);
        const std::vector<ShaderManifest::Entry> shaders = ShaderCooker::PlanTargets();
        progress.cookTotal = static_cast<int>(targets.size() + shaders.size());
        progress.total = static_cast<int>(items.size());

        // 丸ごと写すフォルダとクック済みの DDS のフォルダは、書き出す先の側を消しておく
        std::vector<std::filesystem::path> clearFolders(std::begin(cookedFolders), std::end(cookedFolders));
        for (const FolderItem& folder : folders) {
            clearFolders.push_back(folder.to);
        }
        for (const std::filesystem::path& folder : clearFolders) {
            std::filesystem::remove_all(folder, ec);
            if (ec) {
                result.error = ToUtf8(folder) + " を消せませんでした" + kLockedHint;
                return result;
            }
        }

        // 画像のテクスチャを DDS へ変換する。画像は WIC で読むので、このスレッドで COM を使えるようにする
        struct ComScope
        {
            HRESULT hr;
            ~ComScope() { if (SUCCEEDED(hr)) { ::CoUninitialize(); } }
        } com{ ::CoInitializeEx(nullptr, COINIT_MULTITHREADED) };
        for (const CookTarget& target : targets) {
            const std::string error = TextureCooker::Cook(target, destination, result.fileCount, result.bytes);
            if (!error.empty()) {
                result.error = error;
                return result;
            }
            ++result.cookedTextures;
            ++progress.cooked;
        }

        // 一覧のシェーダをコンパイルして DXIL にする
        for (const ShaderManifest::Entry& shader : shaders) {
            const int filesBefore = result.fileCount;
            const std::string error = ShaderCooker::Cook(shader, destination, result.fileCount, result.bytes);
            if (!error.empty()) {
                result.error = error;
                return result;
            }
            if (result.fileCount > filesBefore) {
                ++result.cookedShaders;
            }
            ++progress.cooked;
        }

        for (const CopyItem& item : items) {
            std::filesystem::create_directories(item.to.parent_path(), ec);
            if (!ec) {
                std::filesystem::copy_file(item.from, item.to, std::filesystem::copy_options::overwrite_existing, ec);
            }
            if (ec) {
                result.error = ToUtf8(item.to) + " を書けませんでした" + kLockedHint;
                return result;
            }
            const std::uintmax_t size = std::filesystem::file_size(item.to, ec);
            result.bytes += ec ? 0 : size;
            ++result.fileCount;
            ++progress.copied;
        }
        return result;
    }
}

#endif // CORE_EDITOR
