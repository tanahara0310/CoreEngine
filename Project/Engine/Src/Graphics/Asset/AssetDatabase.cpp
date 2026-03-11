#include "AssetDatabase.h"
#include "AssetMetadata.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
#include <chrono>

namespace CoreEngine
{
    AssetDatabase& AssetDatabase::GetInstance()
    {
        static AssetDatabase instance;
        return instance;
    }

    void AssetDatabase::Initialize(const std::filesystem::path& projectRoot)
    {
        if (initialized_)
        {
            Logger::GetInstance().Log(
                "AssetDatabase is already initialized",
                LogLevel::WARNING, LogCategory::System
            );
            return;
        }

        projectRoot_ = projectRoot;

        // カテゴリの優先順位を設定（数値が大きいほど優先）
        categoryPriority_["Application"] = 100;
        categoryPriority_["Engine"] = 50;

        Logger::GetInstance().Log(
            "Initializing AssetDatabase at: " + projectRoot_.string(),
            LogLevel::INFO, LogCategory::System
        );

        // アセットディレクトリをスキャン
        std::filesystem::path appAssetsPath = projectRoot_ / "Application" / "Assets";
        std::filesystem::path engineAssetsPath = projectRoot_ / "Engine" / "Assets";

        if (std::filesystem::exists(appAssetsPath))
        {
            ScanDirectory(appAssetsPath, "Application");
        }

        if (std::filesystem::exists(engineAssetsPath))
        {
            ScanDirectory(engineAssetsPath, "Engine");
        }

        initialized_ = true;

        Logger::GetInstance().Log(
            "AssetDatabase initialized. Total assets: " + std::to_string(assetsByGUID_.size()),
            LogLevel::INFO, LogCategory::System
        );
    }

    void AssetDatabase::Finalize()
    {
        assetsByGUID_.clear();
        assetsByName_.clear();
        categoryPriority_.clear();
        initialized_ = false;

        Logger::GetInstance().Log(
            "AssetDatabase finalized",
            LogLevel::INFO, LogCategory::System
        );
    }

    std::string AssetDatabase::FindAssetPath(const std::string& name)
    {
        // まず完全一致で検索
        auto it = assetsByName_.find(name);
        if (it != assetsByName_.end() && !it->second.empty())
        {
            // 複数ある場合は優先順位の高いものを返す
            if (it->second.size() == 1)
            {
                return assetsByGUID_[it->second[0]].relativePath.generic_string();
            }
            else
            {
                // 優先順位でソート
                std::string bestGuid = it->second[0];
                int bestPriority = categoryPriority_[assetsByGUID_[bestGuid].category];

                for (size_t i = 1; i < it->second.size(); ++i)
                {
                    const std::string& guid = it->second[i];
                    int priority = categoryPriority_[assetsByGUID_[guid].category];
                    if (priority > bestPriority)
                    {
                        bestGuid = guid;
                        bestPriority = priority;
                    }
                }

                return assetsByGUID_[bestGuid].relativePath.generic_string();
            }
        }

        // 拡張子なしで検索
        std::string nameWithoutExt = name;
        size_t dotPos = name.find_last_of('.');
        if (dotPos != std::string::npos)
        {
            nameWithoutExt = name.substr(0, dotPos);
        }

        it = assetsByName_.find(nameWithoutExt);
        if (it != assetsByName_.end() && !it->second.empty())
        {
            return assetsByGUID_[it->second[0]].relativePath.generic_string();
        }

        // 見つからない場合は空文字列を返す
        return "";
    }

    std::string AssetDatabase::FindAssetPath(const std::string& name, const std::string& category)
    {
        auto it = assetsByName_.find(name);
        if (it == assetsByName_.end())
        {
            return "";
        }

        // 指定されたカテゴリのアセットを探す
        for (const auto& guid : it->second)
        {
            const auto& assetInfo = assetsByGUID_[guid];
            if (assetInfo.category == category)
            {
                return assetInfo.relativePath.generic_string();
            }
        }

        return "";
    }

    std::string AssetDatabase::FindAssetPathByGUID(const std::string& guid)
    {
        auto it = assetsByGUID_.find(guid);
        if (it != assetsByGUID_.end())
        {
            return it->second.relativePath.generic_string();
        }
        return "";
    }

    const AssetInfo* AssetDatabase::GetAssetInfo(const std::string& nameOrGuid)
    {
        // まずGUIDとして検索
        auto it = assetsByGUID_.find(nameOrGuid);
        if (it != assetsByGUID_.end())
        {
            return &it->second;
        }

        // 名前として検索
        auto nameIt = assetsByName_.find(nameOrGuid);
        if (nameIt != assetsByName_.end() && !nameIt->second.empty())
        {
            return &assetsByGUID_[nameIt->second[0]];
        }

        return nullptr;
    }

    const AssetInfo* AssetDatabase::GetAssetInfoByGUID(const std::string& guid)
    {
        auto it = assetsByGUID_.find(guid);
        if (it != assetsByGUID_.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    std::string AssetDatabase::GetGUID(const std::filesystem::path& assetPath)
    {
        // パスからGUIDを逆引き
        for (const auto& pair : assetsByGUID_)
        {
            if (pair.second.fullPath == assetPath)
            {
                return pair.first;
            }
        }
        return "";
    }

    const std::unordered_map<std::string, AssetInfo>& AssetDatabase::GetAllAssets() const
    {
        return assetsByGUID_;
    }

    void AssetDatabase::Refresh()
    {
        Logger::GetInstance().Log(
            "Refreshing AssetDatabase...",
            LogLevel::INFO, LogCategory::System
        );

        // 一旦クリア
        assetsByGUID_.clear();
        assetsByName_.clear();

        // 再スキャン
        Initialize(projectRoot_);
    }

    void AssetDatabase::UpdateAssetPath(const std::string& guid, const std::filesystem::path& newPath)
    {
        auto it = assetsByGUID_.find(guid);
        if (it == assetsByGUID_.end())
        {
            Logger::GetInstance().Log(
                "Asset not found for GUID: " + guid,
                LogLevel::WARNING, LogCategory::System
            );
            return;
        }

        // パス情報を更新
        it->second.fullPath = newPath;
        it->second.relativePath = std::filesystem::relative(newPath, projectRoot_);
        it->second.fileName = newPath.filename().string();
        it->second.name = newPath.stem().string();
        it->second.lastModified = GetFileLastModified(newPath);

        Logger::GetInstance().Log(
            "Updated asset path for GUID " + guid + ": " + newPath.string(),
            LogLevel::INFO, LogCategory::System
        );
    }

    void AssetDatabase::RegisterAsset(const std::filesystem::path& assetPath, const std::string& category)
    {
        if (!std::filesystem::exists(assetPath) || std::filesystem::is_directory(assetPath))
        {
            return;
        }

        // メタファイルの拡張子は除外
        if (assetPath.extension() == ".meta")
        {
            return;
        }

        AssetType type = GetAssetType(assetPath);
        if (type == AssetType::Unknown)
        {
            return;
        }

        // メタファイルからGUIDを取得または生成
        std::string guid = AssetMetadata::LoadOrCreateMetaFile(assetPath, type);

        // AssetInfo を作成
        AssetInfo info;
        info.guid = guid;
        info.name = assetPath.stem().string();
        info.fileName = assetPath.filename().string();
        info.fullPath = assetPath;
        info.relativePath = std::filesystem::relative(assetPath, projectRoot_);
        info.type = type;
        info.category = category;
        info.lastModified = GetFileLastModified(assetPath);

        // 登録
        assetsByGUID_[guid] = info;
        assetsByName_[info.name].push_back(guid);
    }

    void AssetDatabase::UnregisterAsset(const std::string& guid)
    {
        auto it = assetsByGUID_.find(guid);
        if (it == assetsByGUID_.end())
        {
            return;
        }

        // assetsByName から削除
        const std::string& name = it->second.name;
        auto nameIt = assetsByName_.find(name);
        if (nameIt != assetsByName_.end())
        {
            auto& guids = nameIt->second;
            guids.erase(std::remove(guids.begin(), guids.end(), guid), guids.end());
            if (guids.empty())
            {
                assetsByName_.erase(nameIt);
            }
        }

        // assetsByGUID から削除
        assetsByGUID_.erase(it);

        Logger::GetInstance().Log(
            "Unregistered asset: " + guid,
            LogLevel::INFO, LogCategory::System
        );
    }

    size_t AssetDatabase::GetAssetCountByType(AssetType type) const
    {
        size_t count = 0;
        for (const auto& pair : assetsByGUID_)
        {
            if (pair.second.type == type)
            {
                ++count;
            }
        }
        return count;
    }

    void AssetDatabase::ScanDirectory(const std::filesystem::path& directory, const std::string& category)
    {
        if (!std::filesystem::exists(directory))
        {
            return;
        }

        try
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
            {
                if (entry.is_regular_file())
                {
                    RegisterAsset(entry.path(), category);
                }
            }
        }
        catch (const std::exception& e)
        {
            Logger::GetInstance().Log(
                "Failed to scan directory: " + directory.string() + " (" + e.what() + ")",
                LogLevel::Error, LogCategory::System
            );
        }
    }

    AssetType AssetDatabase::GetAssetType(const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), 
            [](unsigned char c) { return static_cast<char>(::tolower(c)); });

        // テクスチャ
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".dds" || ext == ".hdr")
        {
            return AssetType::Texture;
        }

        // モデル
        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
        {
            return AssetType::Model;
        }

        // シェーダー
        if (ext == ".hlsl" || ext == ".vs" || ext == ".ps" || ext == ".gs" || ext == ".cs")
        {
            return AssetType::Shader;
        }

        // オーディオ
        if (ext == ".wav" || ext == ".mp3" || ext == ".ogg")
        {
            return AssetType::Audio;
        }

        // マテリアル
        if (ext == ".mat" || ext == ".material")
        {
            return AssetType::Material;
        }

        // シーン
        if (ext == ".scene")
        {
            return AssetType::Scene;
        }

        // アニメーション
        if (ext == ".anim" || ext == ".animation")
        {
            return AssetType::Animation;
        }

        return AssetType::Unknown;
    }

    uint64_t AssetDatabase::GetFileLastModified(const std::filesystem::path& path)
    {
        try
        {
            auto ftime = std::filesystem::last_write_time(path);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            return std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
        }
        catch (...)
        {
            return 0;
        }
    }

    std::filesystem::path AssetDatabase::GetLibraryPath() const
    {
        return projectRoot_ / "Library";
    }

    std::filesystem::path AssetDatabase::GetTextureCachePath() const
    {
        std::filesystem::path cachePath = GetLibraryPath() / "TextureCache";

        // フォルダが存在しない場合は作成
        if (!std::filesystem::exists(cachePath))
        {
            try
            {
                std::filesystem::create_directories(cachePath);
                Logger::GetInstance().Log(
                    "Created TextureCache directory: " + cachePath.string(),
                    LogLevel::INFO, LogCategory::System
                );
            }
            catch (const std::exception& e)
            {
                Logger::GetInstance().Log(
                    "Failed to create TextureCache directory: " + std::string(e.what()),
                    LogLevel::Error, LogCategory::System
                );
            }
        }

        return cachePath;
    }

    std::filesystem::path AssetDatabase::GetCachedTexturePath(const std::string& guid, const std::string& extension) const
    {
        return GetTextureCachePath() / (guid + extension);
    }
}
