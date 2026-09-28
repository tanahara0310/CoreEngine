#include "pch.h"
#include "Editor/Launcher/ProjectList.h"

#ifdef CORE_EDITOR

#include "Utility/Path/ProjectPaths.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <format>
#include <fstream>

namespace CoreEngine::Editor
{
    namespace
    {
        constexpr int kVersion = 1;
        constexpr const char* kVersionKey = "version";
        constexpr const char* kOpenLastKey = "openLastOnStartup";
        constexpr const char* kProjectsKey = "projects";
        constexpr const char* kPathKey = "path";
        constexpr const char* kLastOpenedKey = "lastOpened";

        /// 基底クラスのファイル名（スクリプトの数に入れない）
        constexpr const wchar_t* kBaseScriptFileName = L"ScriptComponent.as";

        /// @brief path を UTF-8 の文字列にする（区切りは '/'）
        std::string ToUtf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.generic_u8string();
            return std::string(text.begin(), text.end());
        }

        /// @brief UTF-8 の文字列を path にする
        std::filesystem::path FromUtf8(const std::string& text)
        {
            return std::filesystem::path(std::u8string(text.begin(), text.end()));
        }

        /// @brief 今のローカル時刻（"2026-09-26T12:00:00"）
        std::string NowText()
        {
            const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
            const std::chrono::zoned_time local{ std::chrono::current_zone(), now };
            return std::format("{:%Y-%m-%dT%H:%M:%S}", local);
        }

        /// @brief 区切りをそろえ、末尾の区切りを落とす
        std::filesystem::path Normalized(const std::filesystem::path& folder)
        {
            std::filesystem::path path = folder.lexically_normal();
            if (!path.has_filename() && path.has_relative_path()) {
                path = path.parent_path();
            }
            return path;
        }

        /// @brief path が base の下にあるか
        bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& base)
        {
            const std::filesystem::path relative = Normalized(path).lexically_relative(Normalized(base));
            return !relative.empty() && *relative.begin() != ".." && *relative.begin() != ".";
        }

        /// @brief JSON のオブジェクトの文字列の値（無ければ空）
        std::string StringOf(const nlohmann::json& object, const char* key)
        {
            const auto found = object.find(key);
            return (found != object.end() && found->is_string()) ? found->get<std::string>() : std::string{};
        }
    }

    std::filesystem::path ProjectList::FilePath()
    {
        return ProjectPaths::EngineRoot() / "Saved" / "RecentProjects.json";
    }

    bool ProjectList::IsSameFolder(const std::filesystem::path& a, const std::filesystem::path& b)
    {
        return ::_wcsicmp(Normalized(a).c_str(), Normalized(b).c_str()) == 0;
    }

    void ProjectList::Load()
    {
        records_.clear();
        openLastOnStartup_ = false;

        std::ifstream in(FilePath(), std::ios::binary);
        if (!in) {
            return;
        }
        const nlohmann::json root = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
        if (root.is_discarded() || !root.is_object()) {
            return;
        }

        if (const auto found = root.find(kOpenLastKey); found != root.end() && found->is_boolean()) {
            openLastOnStartup_ = found->get<bool>();
        }
        const auto list = root.find(kProjectsKey);
        if (list == root.end() || !list->is_array()) {
            return;
        }
        for (const auto& item : *list) {
            if (!item.is_object()) {
                continue;
            }
            const std::string path = StringOf(item, kPathKey);
            if (path.empty() || Find(FromUtf8(path))) {
                continue;
            }
            records_.push_back(Record{ Normalized(FromUtf8(path)), StringOf(item, kLastOpenedKey) });
        }
    }

    bool ProjectList::Save() const
    {
        nlohmann::json list = nlohmann::json::array();
        for (const Record& record : records_) {
            nlohmann::json item;
            item[kPathKey] = ToUtf8(record.folder);
            item[kLastOpenedKey] = record.lastOpened;
            list.push_back(std::move(item));
        }

        nlohmann::json root;
        root[kVersionKey] = kVersion;
        root[kOpenLastKey] = openLastOnStartup_;
        root[kProjectsKey] = std::move(list);

        const std::filesystem::path path = FilePath();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << root.dump(4) << '\n';
        return static_cast<bool>(out);
    }

    std::vector<ProjectEntry> ProjectList::Collect() const
    {
        std::vector<ProjectEntry> entries;

        std::error_code ec;
        for (const auto& item : std::filesystem::directory_iterator(ProjectPaths::BundledProjectsDirectory(), ec)) {
            if (item.is_directory(ec) && ProjectPaths::IsProjectFolder(item.path())) {
                entries.push_back(Inspect(item.path()));
            }
        }

        for (const Record& record : records_) {
            auto found = std::find_if(entries.begin(), entries.end(), [&record](const ProjectEntry& entry) {
                return IsSameFolder(entry.folder, record.folder);
                });
            if (found == entries.end()) {
                entries.push_back(Inspect(record.folder));
                found = entries.end() - 1;
            }
            found->lastOpened = record.lastOpened;
        }

        // 開いた日時の新しい順（開いたことが無いものは後ろ）、同じなら名前順
        std::sort(entries.begin(), entries.end(), [](const ProjectEntry& a, const ProjectEntry& b) {
            if (a.lastOpened != b.lastOpened) {
                return a.lastOpened > b.lastOpened;
            }
            return a.name < b.name;
            });
        return entries;
    }

    void ProjectList::MarkOpened(const std::filesystem::path& folder)
    {
        Record* record = Find(folder);
        if (!record) {
            records_.push_back(Record{ Normalized(folder), {} });
            record = &records_.back();
        }
        record->lastOpened = NowText();
    }

    bool ProjectList::Add(const std::filesystem::path& folder)
    {
        if (!ProjectPaths::IsProjectFolder(folder)) {
            return false;
        }
        if (!Find(folder)) {
            records_.push_back(Record{ Normalized(folder), {} });
        }
        return true;
    }

    void ProjectList::Remove(const std::filesystem::path& folder)
    {
        std::erase_if(records_, [&folder](const Record& record) { return IsSameFolder(record.folder, folder); });
    }

    std::filesystem::path ProjectList::LastOpened() const
    {
        const Record* latest = nullptr;
        for (const Record& record : records_) {
            if (!record.lastOpened.empty() && (!latest || record.lastOpened > latest->lastOpened)) {
                latest = &record;
            }
        }
        return latest ? latest->folder : std::filesystem::path{};
    }

    ProjectEntry ProjectList::Inspect(const std::filesystem::path& folder)
    {
        ProjectEntry entry;
        entry.folder = Normalized(folder);
        entry.name = ToUtf8(entry.folder.filename());
        entry.missing = !ProjectPaths::IsProjectFolder(entry.folder);
        entry.bundled = IsWithin(entry.folder, ProjectPaths::BundledProjectsDirectory());
        if (entry.missing) {
            return entry;
        }

        const std::filesystem::path application = entry.folder / "Application";
        if (std::ifstream in(application / "Config" / "EngineSettings" / "Project.json", std::ios::binary); in) {
            const nlohmann::json root = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
            if (!root.is_discarded() && root.is_object()) {
                if (std::string name = StringOf(root, "name"); !name.empty()) {
                    entry.name = std::move(name);
                }
                entry.initialScene = StringOf(root, "initialScene");
            }
        }

        std::error_code ec;
        for (const auto& item : std::filesystem::directory_iterator(application / "Assets" / "Scenes", ec)) {
            if (item.is_directory(ec) && std::filesystem::is_regular_file(item.path() / "_scene.json", ec)) {
                ++entry.sceneCount;
            }
        }

        std::filesystem::recursive_directory_iterator it(application / "Assets" / "Scripts", ec);
        for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == ".as" &&
                ::_wcsicmp(it->path().filename().c_str(), kBaseScriptFileName) != 0) {
                ++entry.scriptCount;
            }
        }
        return entry;
    }

    ProjectList::Record* ProjectList::Find(const std::filesystem::path& folder)
    {
        for (Record& record : records_) {
            if (IsSameFolder(record.folder, folder)) {
                return &record;
            }
        }
        return nullptr;
    }

    const ProjectList::Record* ProjectList::Find(const std::filesystem::path& folder) const
    {
        for (const Record& record : records_) {
            if (IsSameFolder(record.folder, folder)) {
                return &record;
            }
        }
        return nullptr;
    }
}

#endif // CORE_EDITOR
