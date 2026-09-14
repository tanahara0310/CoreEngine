#include "pch.h"
#include "Graphics/Asset/AssetRef.h"

#include "Graphics/Asset/AssetDatabase.h"
#include "Reflection/PropertySerializer.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <filesystem>

namespace CoreEngine
{
    std::string ToAssetPath(const AssetInfo& info)
    {
        std::string path = Logger::GetInstance().PathToUtf8(info.relativePath);
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    }

    const AssetInfo* FindAssetInfo(std::string_view pathOrName, AssetType type)
    {
        if (pathOrName.empty()) {
            return nullptr;
        }

        AssetDatabase& database = AssetDatabase::GetInstance();
        if (pathOrName.find_first_of("/\\") != std::string_view::npos) {
            return database.FindAssetByPath(pathOrName);
        }

        // ファイル名だけなら名前で探す
        const std::filesystem::path found = database.FindAssetPath(std::string(pathOrName), type);
        return found.empty() ? nullptr
            : database.FindAssetByPath(Logger::GetInstance().PathToUtf8(found));
    }

    const AssetInfo* ResolveAssetRef(const Reflection::AssetRefValue& value)
    {
        if (!value.guid.empty()) {
            if (const AssetInfo* info = AssetDatabase::GetInstance().FindAssetByGUID(value.guid)) {
                return info;
            }
        }
        return FindAssetInfo(value.path);
    }

    std::string JsonToAssetPath(const json& node, std::string_view context)
    {
        Reflection::AssetRefValue value;
        if (node.is_string()) {
            value.path = node.get<std::string>();
        } else if (!Reflection::PropertySerializer::JsonToAssetRef(node, value)) {
            return {};
        }
        if (value.guid.empty() && value.path.empty()) {
            return {};
        }

        Logger& log = Logger::GetInstance();
        const AssetInfo* byGuid =
            value.guid.empty() ? nullptr : AssetDatabase::GetInstance().FindAssetByGUID(value.guid);
        const AssetInfo* byPath = value.path.empty() ? nullptr : FindAssetInfo(value.path);

        if (byGuid) {
            if (!value.path.empty() && byPath != byGuid) {
                log.Logf(LogLevel::Warn, LogCategory::Resource,
                    "{}: パス \"{}\" は GUID の指す \"{}\" と食い違っています（GUID を使います）",
                    context, value.path, ToAssetPath(*byGuid));
            }
            return ToAssetPath(*byGuid);
        }

        if (byPath) {
            if (!value.guid.empty()) {
                log.Logf(LogLevel::Warn, LogCategory::Resource,
                    "{}: GUID \"{}\" が見つからないので、パス \"{}\" で引きました",
                    context, value.guid, value.path);
            }
            return ToAssetPath(*byPath);
        }

        log.Logf(LogLevel::Warn, LogCategory::Resource,
            "{}: アセット（GUID \"{}\" / パス \"{}\"）が見つかりません",
            context, value.guid, value.path);
        return value.path;
    }

    json AssetPathToJson(std::string_view path)
    {
        if (path.empty()) {
            return nullptr;
        }
        return Reflection::PropertySerializer::AssetRefToJson(Reflection::AssetRefValue{ {}, std::string(path) });
    }

    std::string AssetRefBase::GetPath() const
    {
        if (const AssetInfo* info = ResolveAssetRef(GetValue())) {
            return ToAssetPath(*info);
        }
        return path_;
    }

    void AssetRefBase::SetValue(const Reflection::AssetRefValue& value)
    {
        guid_ = value.guid;
        path_ = value.path;
    }

    void AssetRefBase::Reset()
    {
        guid_.clear();
        path_.clear();
    }

    void AssetRefBase::AssignPath(std::string_view pathOrName)
    {
        Reset();
        path_.assign(pathOrName.data(), pathOrName.size());
        std::replace(path_.begin(), path_.end(), '\\', '/');

        const AssetInfo* info = FindAssetInfo(path_, type_);
        if (!info) {
            return;
        }
        if (info->type != type_) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                "AssetRef: \"{}\" は {} ではなく {} です",
                path_, AssetTypeToString(type_), AssetTypeToString(info->type));
        }
        guid_ = info->guid;
        path_ = ToAssetPath(*info);
    }
}
