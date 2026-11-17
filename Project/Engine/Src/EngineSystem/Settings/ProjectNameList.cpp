#include "pch.h"
#include "EngineSystem/Settings/ProjectNameList.h"

#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"
#include "Utility/String/FailWith.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utility>

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kVersionKey = "version";
        constexpr const char* kVersion = "1.0";
    }

    ProjectNameList::ProjectNameList(Rules rules)
        : rules_(std::move(rules))
    {
        names_ = Read();
    }

    bool ProjectNameList::Contains(std::string_view name) const
    {
        return std::find(names_.begin(), names_.end(), name) != names_.end();
    }

    bool ProjectNameList::Validate(const std::vector<std::string>& names, std::string* outError) const
    {
        const std::string noun = rules_.noun;
        if (names.empty()) {
            return FailWith(outError, noun + "が 1 つもありません");
        }
        if (rules_.maxCount > 0 && names.size() > rules_.maxCount) {
            return FailWith(outError, noun + "が多すぎます（" + std::to_string(rules_.maxCount) + " まで）");
        }
        if (names.front() != rules_.first) {
            return FailWith(outError, std::string("先頭は ") + rules_.first + " でなければなりません");
        }
        for (const std::string& name : names) {
            if (name.empty()) {
                return FailWith(outError, "空の名前は使えません");
            }
        }
        std::vector<std::string> sorted = names;
        std::sort(sorted.begin(), sorted.end());
        if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
            return FailWith(outError, "同じ名前が 2 つあります");
        }
        if (outError) { outError->clear(); }
        return true;
    }

    bool ProjectNameList::SetNames(std::vector<std::string> names, std::string* outError)
    {
        if (!Validate(names, outError)) {
            return false;
        }
        if (!Write(names)) {
            return FailWith(outError, "保存できませんでした");
        }
        names_ = std::move(names);
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "{}を {} 個にしました", rules_.noun, names_.size());
        return true;
    }

    void ProjectNameList::Reload()
    {
        names_ = Read();
    }

    std::vector<std::string> ProjectNameList::Read() const
    {
        std::vector<std::string> loaded;
        const nlohmann::json root = JsonManager::ParseFile(ProjectPaths::Resolve(rules_.settingsPath));
        if (!root.is_discarded() && root.is_object()) {
            if (const auto found = root.find(rules_.key);
                found != root.end() && found->is_array()) {
                for (const auto& entry : *found) {
                    if (entry.is_string()) {
                        loaded.push_back(entry.get<std::string>());
                    }
                }
            }
        }
        return Validate(loaded) ? loaded : rules_.defaults;
    }

    bool ProjectNameList::Write(const std::vector<std::string>& names) const
    {
        const std::filesystem::path path = ProjectPaths::Resolve(rules_.settingsPath);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }

        nlohmann::json root;
        root[kVersionKey] = kVersion;
        root[rules_.key] = names;

        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return false;
        }
        out << root.dump(4) << '\n';
        return static_cast<bool>(out);
    }
}
