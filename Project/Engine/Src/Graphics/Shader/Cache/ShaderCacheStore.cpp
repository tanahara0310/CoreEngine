#include "pch.h"
#include "ShaderCacheStore.h"
#include "ShaderCacheKey.h"

#include "Utility/Logger/Logger.h"

#include <fstream>

namespace CoreEngine
{
    namespace
    {
        // 依存マニフェストの 1 行は "<64桁の16進> <UTF-8のパス>"。
        // ハッシュを先頭に置くのは、パスに空白が入っても行末まで丸ごとパスとして
        // 読めるようにするため。テキストにしてあるのは
        //「なぜ無効化されなかったのか」を調べるときに目で読めるようにするため
        constexpr size_t kHashHexLength = 64;
    }

    ShaderCacheStore& ShaderCacheStore::GetInstance()
    {
        static ShaderCacheStore instance;
        return instance;
    }

    void ShaderCacheStore::Initialize(const std::filesystem::path& cacheDirectory, bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        cacheDirectory_ = cacheDirectory;
        enabled_ = enabled;
        hitCount_ = 0;
        missCount_ = 0;
        savedBytes_ = 0;
        fileHashCache_.clear();

        if (!enabled_) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Shader,
                "[ShaderCache] 無効（config の shader.enableCache が false）");
            return;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(cacheDirectory_, errorCode);
        if (errorCode) {
            enabled_ = false;
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Shader,
                "[ShaderCache] ディレクトリを作成できないため無効化: {}",
                Logger::GetInstance().PathToUtf8(cacheDirectory_));
            return;
        }

        // 溜まったキャッシュの規模を出す。上限管理はしていないので、
        // 肥大化したら Cache/ShaderCache を手で消せばよい
        uint64_t totalBytes = 0;
        uint32_t fileCount = 0;
        for (const auto& entry : std::filesystem::directory_iterator(cacheDirectory_, errorCode)) {
            if (entry.is_regular_file(errorCode)) {
                totalBytes += entry.file_size(errorCode);
                ++fileCount;
            }
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Shader,
            "[ShaderCache] 有効: {} （既存 {} ファイル / {:.1f} MB）",
            Logger::GetInstance().PathToUtf8(cacheDirectory_),
            fileCount, static_cast<double>(totalBytes) / (1024.0 * 1024.0));
    }

    std::string ShaderCacheStore::MakeFileStem(const EntryInfo& info)
    {
        if (info.primaryKey.empty()) {
            return {};
        }

        // 可読部はシェーダのファイル名（拡張子なし）＋プロファイル。
        // 例: Water.PS.hlsl / ps_6_6 → "Water.PS.ps_6_6-<64桁hex>"
        std::string readable;
        if (!info.sourcePathUtf8.empty()) {
            const size_t slash = info.sourcePathUtf8.find_last_of("/\\");
            std::string fileName = (slash == std::string::npos)
                ? info.sourcePathUtf8
                : info.sourcePathUtf8.substr(slash + 1);

            // 末尾の .hlsl を落とす（Water.PS.hlsl → Water.PS）
            const size_t dot = fileName.find_last_of('.');
            if (dot != std::string::npos && dot > 0) {
                fileName = fileName.substr(0, dot);
            }
            readable = std::move(fileName);
        }

        if (!info.profile.empty()) {
            if (!readable.empty()) {
                readable += ".";
            }
            readable += info.profile;
        }

        // ファイル名に使えない文字を潰す。日本語などの非 ASCII は通す
        // （UTF-8 のまま path へ渡す経路は Logger::Utf8ToPath が担保している）
        for (char& c : readable) {
            switch (c) {
            case '<': case '>': case ':': case '"': case '/':
            case '\\': case '|': case '?': case '*':
                c = '_';
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    c = '_';
                }
                break;
            }
        }

        if (readable.empty()) {
            return info.primaryKey;
        }
        return readable + "-" + info.primaryKey;
    }

    std::filesystem::path ShaderCacheStore::GetBlobPath(const EntryInfo& info) const
    {
        return cacheDirectory_ / Logger::GetInstance().Utf8ToPath(MakeFileStem(info) + ".dxil");
    }

    std::filesystem::path ShaderCacheStore::GetDepsPath(const EntryInfo& info) const
    {
        return cacheDirectory_ / Logger::GetInstance().Utf8ToPath(MakeFileStem(info) + ".deps");
    }

    std::string ShaderCacheStore::GetFileHashCached(const std::filesystem::path& path)
    {
        const std::string key = Logger::GetInstance().PathToUtf8(path);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = fileHashCache_.find(key);
            if (it != fileHashCache_.end()) {
                return it->second;
            }
        }

        // ハッシュ計算はロックの外で行う（同じファイルを二重に計算しうるが結果は同じ）
        std::string hash = ShaderCacheKey::HashFile(path);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            fileHashCache_[key] = hash;
        }
        return hash;
    }

    bool ShaderCacheStore::TryLoad(const EntryInfo& info, std::vector<uint8_t>& blobOut)
    {
        if (!enabled_ || info.primaryKey.empty()) {
            return false;
        }

        const std::filesystem::path depsPath = GetDepsPath(info);
        const std::filesystem::path blobPath = GetBlobPath(info);

        std::error_code errorCode;
        if (!std::filesystem::exists(depsPath, errorCode) ||
            !std::filesystem::exists(blobPath, errorCode)) {
            return false;
        }

        // ===== 依存マニフェストの検証 =====
        // include が 1 つでも中身違い／消失していたらミス扱いにする。
        // ここを甘くすると「.hlsli を直したのに反映されない」という
        // 最悪の壊れ方をする（クラス説明の warning 参照）
        {
            std::ifstream depsFile(depsPath);
            if (!depsFile) {
                return false;
            }

            std::string line;
            while (std::getline(depsFile, line)) {
                if (!line.empty() && line[0] == 0x23) {
                    continue;   // 先頭のコメント行（どのシェーダのものかの目印）
                }
                if (line.size() <= kHashHexLength + 1) {
                    continue;   // 空行や壊れた行は無視
                }

                const std::string recordedHash = line.substr(0, kHashHexLength);
                const std::string dependencyPathUtf8 = line.substr(kHashHexLength + 1);
                const std::filesystem::path dependencyPath =
                    Logger::GetInstance().Utf8ToPath(dependencyPathUtf8);

                const std::string currentHash = GetFileHashCached(dependencyPath);
                if (currentHash.empty() || currentHash != recordedHash) {
                    return false;
                }
            }
        }

        // ===== DXIL 本体の読み出し =====
        std::ifstream blobFile(blobPath, std::ios::binary | std::ios::ate);
        if (!blobFile) {
            return false;
        }

        const std::streamsize size = blobFile.tellg();
        if (size <= 0) {
            return false;
        }
        blobFile.seekg(0, std::ios::beg);

        blobOut.resize(static_cast<size_t>(size));
        if (!blobFile.read(reinterpret_cast<char*>(blobOut.data()), size)) {
            blobOut.clear();
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++hitCount_;
        }
        return true;
    }

    void ShaderCacheStore::Save(
        const EntryInfo& info,
        const void* data,
        size_t size,
        const std::vector<std::filesystem::path>& dependencies)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++missCount_;
        }

        if (!enabled_ || info.primaryKey.empty() || !data || size == 0) {
            return;
        }

        // 依存の中身ハッシュを先に集める。ここで読めないファイルが 1 つでもあれば
        // 検証が常に失敗するキャッシュを書くことになるので、保存自体を諦める
        std::vector<std::pair<std::string, std::string>> dependencyHashes;   // (hash, utf8Path)
        dependencyHashes.reserve(dependencies.size());
        for (const std::filesystem::path& dependency : dependencies) {
            const std::string hash = GetFileHashCached(dependency);
            if (hash.empty()) {
                return;
            }
            dependencyHashes.emplace_back(hash, Logger::GetInstance().PathToUtf8(dependency));
        }

        // DXIL 本体 → .deps の順に書く。逆にすると、途中で落ちたときに
        // 「.deps はあるが .dxil が無い」状態になり、毎回検証を通してから
        // 本体読み出しで失敗する（無駄な I/O が残る）
        const std::filesystem::path blobPath = GetBlobPath(info);
        {
            std::ofstream blobFile(blobPath, std::ios::binary | std::ios::trunc);
            if (!blobFile) {
                return;
            }
            blobFile.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            if (!blobFile) {
                return;
            }
        }

        {
            std::ofstream depsFile(GetDepsPath(info), std::ios::trunc);
            if (!depsFile) {
                std::error_code errorCode;
                std::filesystem::remove(blobPath, errorCode);   // 片割れを残さない
                return;
            }
            // 先頭のコメント行は人間向け。キーはハッシュ文字列なので、これが無いと
            // どのファイルがどのシェーダのキャッシュか分からない（TryLoad 側は読み飛ばす）
            depsFile << "# " << info.sourcePathUtf8 << " (" << info.profile << ")\n";
            for (const auto& entry : dependencyHashes) {
                depsFile << entry.first << " " << entry.second << "\n";
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            savedBytes_ += size;
        }
    }

    void ShaderCacheStore::LogSummary()
    {
        if (!enabled_) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        const uint32_t total = hitCount_ + missCount_;
        const double hitRate = (total > 0) ? (100.0 * hitCount_ / total) : 0.0;

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Shader,
            "[ShaderCache] ヒット {} / ミス {} （ヒット率 {:.1f}%） 新規保存 {:.2f} MB",
            hitCount_, missCount_, hitRate,
            static_cast<double>(savedBytes_) / (1024.0 * 1024.0));
    }
}
