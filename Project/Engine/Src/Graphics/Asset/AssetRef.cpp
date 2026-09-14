#include "pch.h"
#include "Graphics/Asset/AssetRef.h"

#include "Graphics/Asset/AssetDatabase.h"
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

    const AssetInfo* FindAssetInfo(std::string_view pathOrName)
    {
        if (pathOrName.empty()) {
            return nullptr;
        }

        AssetDatabase& database = AssetDatabase::GetInstance();
        if (pathOrName.find_first_of("/\\") != std::string_view::npos) {
            return database.FindAssetByPath(pathOrName);
        }

        // ファイル名だけなら名前で探す
        const std::filesystem::path found = database.FindAssetPath(std::string(pathOrName));
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

        const AssetInfo* info = FindAssetInfo(path_);
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
