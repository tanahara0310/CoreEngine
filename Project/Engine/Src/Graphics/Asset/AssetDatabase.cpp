#include "pch.h"
#include "AssetDatabase.h"
#include "Utility/Path/ProjectPaths.h"
#include "AssetMetadata.h"
#include "Threading/ThreadPool.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
#include <chrono>
#include <optional>
#include <string_view>

namespace CoreEngine
{
    namespace
    {
        /// @brief パスを照合用のキーにする（区切りを '/'、ASCII の英字を小文字へ）
        std::string MakePathKey(std::string_view path)
        {
            std::string key(path);
            for (char& c : key) {
                if (c == '\\') {
                    c = '/';
                } else if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            return key;
        }

        /// @brief `Assets/Scenes/` の下のファイル（シーンの保存データ）か
        bool IsSceneSaveData(const std::filesystem::path& path)
        {
            bool afterAssets = false;
            for (const auto& part : path) {
                const std::string name = MakePathKey(Logger::GetInstance().PathToUtf8(part));
                if (afterAssets && name == "scenes") {
                    return true;
                }
                afterAssets = name == "assets";
            }
            return false;
        }
    }

    AssetDatabase& AssetDatabase::GetInstance()
    {
        static AssetDatabase instance;
        return instance;
    }

    void AssetDatabase::Initialize()
    {
        if (initialized_)
        {
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::System, "{}",
                "AssetDatabase is already initialized");
            return;
        }

        // カテゴリの優先順位を設定（数値が大きいほど優先）
        categoryPriority_["Application"] = 100;
        categoryPriority_["Engine"] = 50;

        // スキャン対象ディレクトリを収集する。
        struct ScanTarget {
            std::filesystem::path path;
            std::string category;
        };
        std::vector<ScanTarget> targets;

        const std::filesystem::path appAssetsPath = ProjectPaths::Resolve("Application/Assets");
        const std::filesystem::path engineAssetsPath = ProjectPaths::Resolve("Engine/Assets");

        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::System,
            "アセットデータベースを作ります（プロジェクト: {} ／ エンジン: {}）",
            Logger::GetInstance().PathToUtf8(appAssetsPath),
            Logger::GetInstance().PathToUtf8(engineAssetsPath));

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

        // ワーカー数は ThreadBudget に決めさせる（プール乱立でコア数を超えないため）。
        // このプールはスキャン完了後に解放するので、枠は他のプールへ返る。
        ThreadPoolDesc poolDesc;
        poolDesc.name = "AssetScan";
        poolDesc.priority = WorkerPriority::Normal;   // 起動をブロックするので譲らない
        threadPool_ = std::make_unique<ThreadPool>(poolDesc);

        // フェーズ1: 全ディレクトリのファイル列挙を並列に実行する。
        struct FileEntry {
            std::filesystem::path filePath;
            std::string category;
        };

        std::vector<std::future<std::vector<FileEntry>>> scanFutures;
        scanFutures.reserve(targets.size());

        for (const auto& target : targets) {
            scanFutures.push_back(threadPool_->Submit(
                "Scan: " + target.category,
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
            // Wait はブロックする代わりにキューのタスクを引き受ける。
            // メインスレッドを遊ばせず fan-out の一部を肩代わりする
            threadPool_->Wait(future);
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
                "AssetInfo: " + Logger::GetInstance().PathToUtf8(file.filePath.filename()),
                [this, file]() -> std::optional<AssetInfo> {
                    return BuildAssetInfo(file.filePath, file.category);
                }
            ));
        }

        // フェーズ3: 全結果をメインスレッドでインデックスに登録する。
        for (auto& future : buildFutures) {
            threadPool_->Wait(future);
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
        guidsByPath_.clear();
        categoryPriority_.clear();
        initialized_ = false;

        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::System, "{}",
            "AssetDatabase finalized");
    }

    std::filesystem::path AssetDatabase::FindAssetPath(const std::string& name)
    {
        return FindAssetPath(name, AssetType::Unknown);
    }

    std::filesystem::path AssetDatabase::FindAssetPath(const std::string& name, AssetType type)
    {
        const auto matches = [this, type](const std::string& guid) {
            return type == AssetType::Unknown || assetsByGUID_[guid].type == type;
        };

        // まず完全一致で検索（複数ある場合は優先順位の高いもの、同じ優先順位なら先に登録したもの）
        auto it = assetsByName_.find(name);
        if (it != assetsByName_.end())
        {
            const std::string* bestGuid = nullptr;
            int bestPriority = 0;
            for (const std::string& guid : it->second)
            {
                if (!matches(guid))
                {
                    continue;
                }
                const int priority = categoryPriority_[assetsByGUID_[guid].category];
                if (!bestGuid || priority > bestPriority)
                {
                    bestGuid = &guid;
                    bestPriority = priority;
                }
            }
            if (bestGuid)
            {
                return assetsByGUID_[*bestGuid].fullPath;
            }
        }

        // 拡張子なしで検索（先に登録したもの）
        std::string nameWithoutExt = name;
        size_t dotPos = name.find_last_of('.');
        if (dotPos != std::string::npos)
        {
            nameWithoutExt = name.substr(0, dotPos);
        }

        it = assetsByName_.find(nameWithoutExt);
        if (it != assetsByName_.end())
        {
            for (const std::string& guid : it->second)
            {
                if (matches(guid))
                {
                    return assetsByGUID_[guid].fullPath;
                }
            }
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

    const AssetInfo* AssetDatabase::FindAssetByGUID(const std::string& guid) const
    {
        const auto it = assetsByGUID_.find(guid);
        return it != assetsByGUID_.end() ? &it->second : nullptr;
    }

    const AssetInfo* AssetDatabase::FindAssetByPath(std::string_view path) const
    {
        if (path.empty()) {
            return nullptr;
        }

        Logger& log = Logger::GetInstance();
        std::string relative(path);
        const std::filesystem::path asPath = log.Utf8ToPath(relative);
        if (asPath.is_absolute()) {
            // 綴りへ直す（どちらの根の下でもなければ見つからない扱い）
            const std::filesystem::path spelled = ProjectPaths::MakeRelative(asPath);
            if (spelled.empty()) {
                return nullptr;
            }
            relative = log.PathToUtf8(spelled);
        }

        const std::string key = MakePathKey(relative);
        auto it = guidsByPath_.find(key);
        if (it == guidsByPath_.end() &&
            !key.starts_with("application/assets/") && !key.starts_with("engine/assets/")) {
            // Application/Assets/ を省いた相対パスとして引き直す
            it = guidsByPath_.find("application/assets/" + key);
        }
        return it != guidsByPath_.end() ? FindAssetByGUID(it->second) : nullptr;
    }

    std::vector<const AssetInfo*> AssetDatabase::GetAssetsOfType(AssetType type) const
    {
        std::vector<const AssetInfo*> assets;
        for (const auto& [guid, info] : assetsByGUID_) {
            if (info.type == type) {
                assets.push_back(&info);
            }
        }
        std::sort(assets.begin(), assets.end(),
            [](const AssetInfo* a, const AssetInfo* b) { return a->relativePath < b->relativePath; });
        return assets;
    }

    const AssetInfo* AssetDatabase::ImportAsset(const std::filesystem::path& assetPath)
    {
        Logger& log = Logger::GetInstance();
        const std::filesystem::path fullPath = (assetPath.is_absolute()
            ? assetPath
            : ProjectPaths::Resolve(log.PathToUtf8(assetPath))).lexically_normal();
        if (const AssetInfo* existing = FindAssetByPath(log.PathToUtf8(fullPath))) {
            return existing;
        }

        // Engine/Assets の下なら Engine、Application/Assets の下なら Application のアセットとして登録する
        const std::string key = MakePathKey(log.PathToUtf8(ProjectPaths::MakeRelative(fullPath)));
        if (key.empty()) {
            return nullptr;
        }
        const std::string category = key.starts_with("engine/") ? "Engine" : "Application";
        std::optional<AssetInfo> info = BuildAssetInfo(fullPath, category);
        if (!info) {
            return nullptr;
        }

        const std::string guid = info->guid;
        MergeAssetInfo(std::move(*info));
        ++revision_;
        return FindAssetByGUID(guid);
    }

    void AssetDatabase::Refresh()
    {
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::System, "{}",
            "Refreshing AssetDatabase...");

        initialized_ = false;
        assetsByGUID_.clear();
        assetsByName_.clear();
        guidsByPath_.clear();

        Initialize();
        ++revision_;
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

        // 綴り（Application/… か Engine/…）で引けないファイルは登録しない
        std::filesystem::path relativePath = ProjectPaths::MakeRelative(assetPath);
        if (relativePath.empty())
        {
            return std::nullopt;
        }

        // メタファイルからGUIDを取得または生成（ファイルI/O）
        std::string guid = AssetMetadata::LoadOrCreateMetaFile(assetPath, type);
        if (guid.empty())
        {
            return std::nullopt;
        }

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
        info.relativePath = std::move(relativePath);
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
        Logger& log = Logger::GetInstance();
        const std::string pathKey = MakePathKey(log.PathToUtf8(info.relativePath));

        assetsByGUID_[guid] = std::move(info);

        // 綴り（Application/… か Engine/…）で登録
        guidsByPath_[pathKey] = guid;

        // ベース名（例: GrayScale）で登録
        assetsByName_[name].push_back(guid);

        // ファイル名フル（例: GrayScale.CS.hlsl）で登録
        assetsByName_[fileName].push_back(guid);

        // 中間 stem（例: GrayScale.CS）でも検索できるよう全 stem を登録
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

        // 物理の材質
        if (ext == ".physmat")
        {
            return AssetType::PhysicsMaterial;
        }

        // プレハブ
        if (ext == ".prefab")
        {
            return AssetType::Prefab;
        }

        // アニメーション
        if (ext == ".anim" || ext == ".animation")
        {
            return AssetType::Animation;
        }

        // モデルのマテリアル定義（.obj から参照される）
        if (ext == ".mtl")
        {
            return AssetType::MaterialLibrary;
        }

        // 表形式のデータ
        if (ext == ".csv")
        {
            return AssetType::Csv;
        }

        // JSON のデータ（シーンの保存データは登録しない）
        if (ext == ".json")
        {
            return IsSceneSaveData(path) ? AssetType::Unknown : AssetType::Json;
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
        // アセットから作り直せる派生物（テクスチャキャッシュ等）の置き場
        return ProjectPaths::Intermediate();
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
        // 並びはパスの順にそろえる
        std::sort(dirs.begin(), dirs.end());
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


