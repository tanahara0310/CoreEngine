#include "pch.h"
#include "AssetDatabase.h"
#include "AssetMetadata.h"
#include "Threading/ThreadPool.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
#include <chrono>
#include <optional>

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
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::System, "{}",
                "AssetDatabase is already initialized");
            return;
        }

        projectRoot_ = projectRoot;

        // カテゴリの優先順位を設定（数値が大きいほど優先）
        categoryPriority_["Application"] = 100;
        categoryPriority_["Engine"] = 50;

        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::System, "{}",
            "Initializing AssetDatabase at: " + projectRoot_.string());

        // スキャン対象ディレクトリを収集する。
        struct ScanTarget {
            std::filesystem::path path;
            std::string category;
        };
        std::vector<ScanTarget> targets;

        std::filesystem::path appAssetsPath = projectRoot_ / "Application" / "Assets";
        std::filesystem::path engineAssetsPath = projectRoot_ / "Engine" / "Assets";

        if (std::filesystem::exists(appAssetsPath)) {
            targets.push_back({ appAssetsPath, "Application" });
        }
        if (std::filesystem::exists(engineAssetsPath)) {
            targets.push_back({ engineAssetsPath, "Engine" });
        }

        if (targets.empty()) {
            initialized_ = true;
            return;
        }

        threadPool_ = std::make_unique<ThreadPool>(
            static_cast<uint32_t>((std::max)(1u, std::thread::hardware_concurrency() / 2)));

        // フェーズ1: 全ディレクトリのファイル列挙を並列に実行する。
        struct FileEntry {
            std::filesystem::path filePath;
            std::string category;
        };

        std::vector<std::future<std::vector<FileEntry>>> scanFutures;
        scanFutures.reserve(targets.size());

        for (const auto& target : targets) {
            scanFutures.push_back(threadPool_->Submit(
                [target]() -> std::vector<FileEntry> {
                    std::vector<FileEntry> files;
                    try {
                        for (const auto& entry : std::filesystem::recursive_directory_iterator(target.path)) {
                            if (entry.is_regular_file()) {
                                files.push_back({ entry.path(), target.category });
                            }
                        }
                    }
                    catch (const std::exception& e) {
                        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System, "{}",
                            "Failed to scan directory: " + target.path.string() + " (" + e.what() + ")");
                    }
                    return files;
                }
            ));
        }

        // 全ディレクトリの結果を統合する。
        std::vector<FileEntry> allFiles;
        for (auto& future : scanFutures) {
            auto files = future.get();
            allFiles.insert(allFiles.end(),
                std::make_move_iterator(files.begin()),
                std::make_move_iterator(files.end()));
        }

        // フェーズ2: 全ファイルの AssetInfo 構築を一括で並列実行する。
        std::vector<std::future<std::optional<AssetInfo>>> buildFutures;
        buildFutures.reserve(allFiles.size());

        for (const auto& file : allFiles) {
            buildFutures.push_back(threadPool_->Submit(
                [this, file]() -> std::optional<AssetInfo> {
                    return BuildAssetInfo(file.filePath, file.category);
                }
            ));
        }

        // フェーズ3: 全結果をメインスレッドでインデックスに登録する。
        for (auto& future : buildFutures) {
            auto result = future.get();
            if (result.has_value()) {
                MergeAssetInfo(std::move(result.value()));
            }
        }

        threadPool_->Shutdown();
        threadPool_.reset();

        initialized_ = true;

        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::System, "{}",
            "AssetDatabase initialized. Total assets: " + std::to_string(assetsByGUID_.size()));
    }

    void AssetDatabase::Finalize()
    {
        assetsByGUID_.clear();
        assetsByName_.clear();
        categoryPriority_.clear();
        initialized_ = false;

        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::System, "{}",
            "AssetDatabase finalized");
    }

    std::filesystem::path AssetDatabase::FindAssetPath(const std::string& name)
    {
        // まず完全一致で検索
        auto it = assetsByName_.find(name);
        if (it != assetsByName_.end() && !it->second.empty())
        {
            // 複数ある場合は優先順位の高いものを返す
            if (it->second.size() == 1)
            {
                return assetsByGUID_[it->second[0]].fullPath;
            } else
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

                return assetsByGUID_[bestGuid].fullPath;
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
            return assetsByGUID_[it->second[0]].fullPath;
        }

        // 見つからない場合は空の path を返す
        return {};
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

    void AssetDatabase::Refresh()
    {
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::System, "{}",
            "Refreshing AssetDatabase...");

        initialized_ = false;
        assetsByGUID_.clear();
        assetsByName_.clear();

        Initialize(projectRoot_);
    }

    std::optional<AssetInfo> AssetDatabase::BuildAssetInfo(
        const std::filesystem::path& assetPath,
        const std::string& category) const
    {
        if (!std::filesystem::exists(assetPath) || std::filesystem::is_directory(assetPath))
        {
            return std::nullopt;
        }

        // メタファイルの拡張子は除外
        if (assetPath.extension() == ".meta")
        {
            return std::nullopt;
        }

        AssetType type = GetAssetType(assetPath);
        if (type == AssetType::Unknown)
        {
            return std::nullopt;
        }

        // メタファイルからGUIDを取得または生成（ファイルI/O）
        std::string guid = AssetMetadata::LoadOrCreateMetaFile(assetPath, type);

        AssetInfo info;
        info.guid = guid;
        // 検索キーになる name / fileName は UTF-8 で登録する。
        // 呼び出し側の検索名も UTF-8 に統一してあり、path::string()（ANSI）で
        // 登録すると非 ASCII のファイル名で一致しなくなる。
        Logger& log = Logger::GetInstance();
        // 複合拡張子（例: GrayScale.CS.hlsl）に対してもベース名（GrayScale）で検索できるよう
        // stem を繰り返し適用して最初のドットより前の名前を取得する。
        {
            std::filesystem::path stem = assetPath.stem();
            while (stem.has_extension()) {
                stem = stem.stem();
            }
            info.name = log.PathToUtf8(stem);
        }
        info.fileName = log.PathToUtf8(assetPath.filename());
        info.fullPath = assetPath;
        info.relativePath = std::filesystem::relative(assetPath, projectRoot_);
        info.type = type;
        info.category = category;
        info.lastModified = GetFileLastModified(assetPath);

        return info;
    }

    void AssetDatabase::MergeAssetInfo(AssetInfo&& info)
    {
        const std::string guid = info.guid;
        const std::string name = info.name;
        const std::string fileName = info.fileName;

        assetsByGUID_[guid] = std::move(info);

        // ベース名（例: GrayScale）で登録
        assetsByName_[name].push_back(guid);

        // ファイル名フル（例: GrayScale.CS.hlsl）で登録
        assetsByName_[fileName].push_back(guid);

        // 中間 stem（例: GrayScale.CS）でも検索できるよう全 stem を登録
        Logger& log = Logger::GetInstance();
        std::filesystem::path stem = log.Utf8ToPath(fileName).stem();
        while (stem.has_extension()) {
            assetsByName_[log.PathToUtf8(stem)].push_back(guid);
            stem = stem.stem();
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

        // モデル（.fbx を含む）
        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
        {
            return AssetType::Model;
        }

        // シェーダー（.cs は C# ファイルと重複するため除外）
        if (ext == ".hlsl" || ext == ".hlsli" || ext == ".vs" || ext == ".ps" || ext == ".gs")
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
        return projectRoot_ / "Cache";
    }

    std::vector<std::filesystem::path> AssetDatabase::GetShaderIncludeDirectories() const
    {
        std::vector<std::filesystem::path> dirs;
        for (const auto& [guid, info] : assetsByGUID_)
        {
            if (info.type != AssetType::Shader)
            {
                continue;
            }
            auto dir = info.fullPath.parent_path();
            if (std::find(dirs.begin(), dirs.end(), dir) == dirs.end())
            {
                dirs.push_back(dir);
            }
        }
        return dirs;
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
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::System, "{}",
                    "Created TextureCache directory: " + cachePath.string());
            }
            catch (const std::exception& e)
            {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System, "{}",
                    "Failed to create TextureCache directory: " + std::string(e.what()));
            }
        }

        return cachePath;
    }

    std::filesystem::path AssetDatabase::GetCachedTexturePath(const std::string& guid, const std::string& extension) const
    {
        return GetTextureCachePath() / (guid + extension);
    }
}


