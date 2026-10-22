#include "pch.h"
#include "DerivedData.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Shader/Cache/ShaderCacheKey.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/CookedPath.h"
#include "Utility/Path/ProjectPaths.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kInfoExtension = ".ddinfo";
        constexpr size_t kHashChunkSize = 1 << 20;
        constexpr size_t kPathHashLength = 16;

        /// @brief Origin ごとの件数
        struct Counts
        {
            uint32_t cooked = 0;
            uint32_t intermediate = 0;
            uint32_t imported = 0;
            uint32_t missing = 0;
        };

        std::mutex g_countsMutex;
        std::map<std::string, Counts> g_counts;

        // 変換中の置き場（同じ置き場を 2 つのスレッドが同時に書かない）
        std::mutex g_importMutex;
        std::condition_variable g_importDone;
        std::set<std::filesystem::path> g_importing;

        /// @brief 元のファイルの大きさと最終更新日時
        struct SourceStamp
        {
            uintmax_t size = 0;
            int64_t time = 0;
            bool valid = false;
        };

        void Count(const std::string& kind, DerivedData::Origin origin)
        {
            std::lock_guard lock(g_countsMutex);
            Counts& counts = g_counts[kind];
            switch (origin) {
            case DerivedData::Origin::Cooked: ++counts.cooked; break;
            case DerivedData::Origin::Intermediate: ++counts.intermediate; break;
            case DerivedData::Origin::Imported: ++counts.imported; break;
            case DerivedData::Origin::Missing: ++counts.missing; break;
            }
        }

        std::string ToUtf8(const std::filesystem::path& path)
        {
            return Logger::GetInstance().PathToUtf8(path);
        }

        std::filesystem::path InfoPath(const std::filesystem::path& derived)
        {
            std::filesystem::path info = derived;
            info += kInfoExtension;
            return info;
        }

        SourceStamp Stamp(const std::filesystem::path& source)
        {
            SourceStamp stamp;
            std::error_code ec;
            stamp.size = std::filesystem::file_size(source, ec);
            if (ec) {
                return stamp;
            }
            const auto time = std::filesystem::last_write_time(source, ec);
            if (ec) {
                return stamp;
            }
            stamp.time = static_cast<int64_t>(time.time_since_epoch().count());
            stamp.valid = true;
            return stamp;
        }

        /// @brief 派生キー（元のファイルの中身・種類・設定・版の SHA-256。読めなければ空）
        std::string ComputeKey(const DerivedData::Request& request)
        {
            std::ifstream file(request.source, std::ios::binary);
            if (!file) {
                return {};
            }
            Sha256Hasher hasher;
            std::vector<char> buffer(kHashChunkSize);
            while (file) {
                file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize read = file.gcount();
                if (read > 0) {
                    hasher.Update(buffer.data(), static_cast<size_t>(read));
                }
            }
            hasher.Update(std::format("\n{}\n{}\n{}", request.kind, request.settings, request.importerVersion));
            return hasher.Finish();
        }

        void WriteInfo(const DerivedData::Request& request, const std::string& key, const SourceStamp& stamp)
        {
            const std::filesystem::path relative = ProjectPaths::MakeRelative(request.source);
            const json data = {
                { "source", ToUtf8(relative.empty() ? request.source : relative) },
                { "settings", request.settings },
                { "importerVersion", request.importerVersion },
                { "size", stamp.size },
                { "time", stamp.time },
                { "key", key },
            };
            JsonManager::GetInstance().SaveJson(ToUtf8(InfoPath(request.intermediate)), data);
        }

        /// @brief 開発中の置き場の派生データが元のファイルと設定に合っていれば、その派生キーを返す（合わなければ空）
        std::string FreshKey(const DerivedData::Request& request)
        {
            std::error_code ec;
            const std::filesystem::path info = InfoPath(request.intermediate);
            if (!std::filesystem::is_regular_file(request.intermediate, ec) || !std::filesystem::is_regular_file(info, ec)) {
                return {};
            }
            const json data = JsonManager::GetInstance().LoadJson(ToUtf8(info));
            if (!data.is_object()) {
                return {};
            }
            const std::string key = JsonManager::SafeGet<std::string>(data, "key", "");
            if (key.empty() ||
                JsonManager::SafeGet<std::string>(data, "settings", "") != request.settings ||
                JsonManager::SafeGet<uint32_t>(data, "importerVersion", 0) != request.importerVersion) {
                return {};
            }

            const SourceStamp stamp = Stamp(request.source);
            if (!stamp.valid) {
                return {};
            }
            if (JsonManager::SafeGet<uintmax_t>(data, "size", 0) == stamp.size &&
                JsonManager::SafeGet<int64_t>(data, "time", 0) == stamp.time) {
                return key;
            }

            // 大きさか日時が違えば中身で比べ、同じなら控えの大きさと日時を書き直す
            if (ComputeKey(request) != key) {
                return {};
            }
            WriteInfo(request, key, stamp);
            return key;
        }

        /// @brief 置き場 1 つの変換の権利（取れるまで待つ）
        class ImportGuard
        {
        public:
            explicit ImportGuard(std::filesystem::path path) : path_(std::move(path))
            {
                std::unique_lock lock(g_importMutex);
                g_importDone.wait(lock, [this] { return !g_importing.contains(path_); });
                g_importing.insert(path_);
            }

            ~ImportGuard()
            {
                {
                    std::lock_guard lock(g_importMutex);
                    g_importing.erase(path_);
                }
                g_importDone.notify_all();
            }

            ImportGuard(const ImportGuard&) = delete;
            ImportGuard& operator=(const ImportGuard&) = delete;

        private:
            std::filesystem::path path_;
        };
    }

    DerivedData::Result DerivedData::EnsureIntermediate(const Request& request, const Importer& importer)
    {
        Logger& log = Logger::GetInstance();
        if (request.intermediate.empty()) {
            return {};
        }

        ImportGuard guard(request.intermediate);
        if (std::string key = FreshKey(request); !key.empty()) {
            return { request.intermediate, Origin::Intermediate, std::move(key) };
        }

        const SourceStamp stamp = Stamp(request.source);
        const std::string key = ComputeKey(request);
        if (!stamp.valid || key.empty()) {
            log.Logf(LogLevel::WARNING, LogCategory::Resource, "元のファイルを読めないので派生データを作れません: {}", ToUtf8(request.source));
            return {};
        }

        // 別の名前に書いてから置き換える（読む側が書きかけの物を開かない）
        std::error_code ec;
        std::filesystem::create_directories(request.intermediate.parent_path(), ec);
        std::filesystem::path temp = request.intermediate;
        temp += std::format(L".{}.tmp", ::GetCurrentProcessId());
        std::filesystem::remove(temp, ec);
        if (!importer(request.source, temp)) {
            std::filesystem::remove(temp, ec);
            return {};
        }
        std::filesystem::rename(temp, request.intermediate, ec);
        if (ec) {
            std::error_code removeError;
            std::filesystem::remove(temp, removeError);
            log.Logf(LogLevel::WARNING, LogCategory::Resource, "派生データを置けません: {}", ToUtf8(request.intermediate));
            return {};
        }
        WriteInfo(request, key, stamp);
        log.Logf(LogLevel::INFO, LogCategory::Resource, "派生データを作りました: {} -> {}",
            ToUtf8(request.source), ToUtf8(request.intermediate));
        return { request.intermediate, Origin::Imported, key };
    }

    DerivedData::Result DerivedData::Find(const Request& request, const Importer& importer)
    {
        Result result;
        std::error_code ec;
        Logger& log = Logger::GetInstance();
        if (!request.cooked.empty() && std::filesystem::is_regular_file(request.cooked, ec)) {
            result = { request.cooked, Origin::Cooked, {} };
        } else if (!request.cookedFallback.empty() && std::filesystem::is_regular_file(request.cookedFallback, ec)) {
            log.Logf(LogLevel::WARNING, LogCategory::Resource, "頼まれた設定の版が書き出されていないので、もう一方の版で代用します: {}",
                ToUtf8(request.cookedFallback));
            result = { request.cookedFallback, Origin::Cooked, {} };
        } else if (CookedPath::IsCookedBuild()) {
            log.Logf(LogLevel::Error, LogCategory::Resource, "書き出したゲームに派生データがありません（書き出し直してください）: {}",
                ToUtf8(request.source));
        } else {
            result = EnsureIntermediate(request, importer);
        }
        Count(request.kind, result.origin);
        return result;
    }

    std::filesystem::path DerivedData::IntermediateDirectory(std::string_view kind)
    {
        return ProjectPaths::Intermediate(std::string("DerivedData/") + std::string(kind));
    }

    std::filesystem::path DerivedData::IntermediatePath(std::string_view kind, const std::filesystem::path& source,
        std::string_view extension)
    {
        std::string name = AssetDatabase::GetInstance().GetGUID(source);
        if (name.empty()) {
            const std::filesystem::path relative = ProjectPaths::MakeRelative(source);
            std::string spelled = ToUtf8(relative.empty() ? source.lexically_normal() : relative);
            std::replace(spelled.begin(), spelled.end(), '\\', '/');
            std::transform(spelled.begin(), spelled.end(), spelled.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            Sha256Hasher hasher;
            hasher.Update(spelled);
            name = "path-" + hasher.Finish().substr(0, kPathHashLength);
        }
        name += extension;
        return IntermediateDirectory(kind) / name;
    }

    void DerivedData::LogSummary()
    {
        Logger& log = Logger::GetInstance();
        std::lock_guard lock(g_countsMutex);
        for (const auto& [kind, counts] : g_counts) {
            log.Logf(LogLevel::INFO, LogCategory::Resource, "[DerivedData] {}: クック済み {} / 中間 {} / 取り込み {} / 無し {}",
                kind, counts.cooked, counts.intermediate, counts.imported, counts.missing);
        }
    }
}
