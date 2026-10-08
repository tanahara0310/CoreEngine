#include "pch.h"
#include "ShaderManifest.h"

#include <fstream>
#include <sstream>

#include "Graphics/Asset/AssetDatabase.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

namespace
{
    constexpr char kHeaderLine1[] =
        "# CoreEngine shader manifest - 実行時に記録された「実際にコンパイルされるシェーダ」の一覧";
    constexpr char kHeaderLine2[] =
        "# 形式: <profile>|<entryPoint>|<綴り>   entryPoint が空ならライブラリ(-E なし)";
    constexpr char kHeaderLine3[] =
        "# 自動生成。起動と終了のたびに、使ったシェーダを足して書き直す（ファイルが無くなったものは落とす）";

    /// @brief 解決済みの絶対パスを綴り（`Engine/Assets/…`）にする。根の外なら絶対パスのまま
    std::wstring ToManifestPath(const std::filesystem::path& absolute)
    {
        const std::filesystem::path relative = CoreEngine::ProjectPaths::MakeRelative(absolute);
        return (relative.empty() ? absolute : relative).generic_wstring();
    }

    /// @brief 一覧の 1 行のパスを綴りにそろえる。ファイルが見つからなければ空
    /// @details 綴り・絶対パス・ファイル名だけ（以前の一覧）のどれでも受け付ける
    std::wstring NormalizeManifestPath(const std::wstring& filePath)
    {
        CoreEngine::Logger& log = CoreEngine::Logger::GetInstance();
        std::filesystem::path path(filePath);
        if (path.is_relative()) {
            path = CoreEngine::ProjectPaths::Resolve(log.PathToUtf8(path));
        }
        std::error_code errorCode;
        if (!std::filesystem::is_regular_file(path, errorCode)) {
            path = CoreEngine::AssetDatabase::GetInstance().FindAssetPath(
                log.PathToUtf8(std::filesystem::path(filePath).filename()));
            if (path.empty() || !std::filesystem::is_regular_file(path, errorCode)) {
                return {};
            }
        }
        return ToManifestPath(path);
    }
}

namespace CoreEngine
{
    ShaderManifest& ShaderManifest::GetInstance()
    {
        static ShaderManifest instance;
        return instance;
    }

    void ShaderManifest::Initialize(const std::filesystem::path& manifestPath, bool enabled)
    {
        path_ = manifestPath;
        enabled_ = enabled;

        if (!enabled_) {
            return;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(path_.parent_path(), errorCode);
    }

    // コンパイル要求を 1 件記録する（重複は set が吸収する）
    void ShaderManifest::Record(const std::wstring& resolvedPath,
        const wchar_t* profile,
        const wchar_t* entryPoint)
    {
        if (!enabled_ || resolvedPath.empty()) {
            return;
        }

        Entry entry;
        entry.filePath = ToManifestPath(resolvedPath);
        entry.profile = profile ? profile : L"";
        entry.entryPoint = entryPoint ? entryPoint : L"";

        std::lock_guard<std::mutex> lock(mutex_);
        recorded_.insert(std::move(entry));
    }

    // 前回の実行で記録した一覧を読む。壊れた行は黙って捨てる
    // （事前コンパイルの入力にすぎず、失敗しても直列コンパイルに落ちるだけ）
    std::vector<ShaderManifest::Entry> ShaderManifest::Load() const
    {
        std::vector<Entry> entries;

        if (!enabled_) {
            return entries;
        }

        std::ifstream file(path_);
        if (!file) {
            return entries;   // 初回起動。記録が無いので事前コンパイルはしない
        }

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // <profile>|<entryPoint>|<path>
            const size_t first = line.find('|');
            if (first == std::string::npos) {
                continue;
            }
            const size_t second = line.find('|', first + 1);
            if (second == std::string::npos) {
                continue;
            }

            Entry entry;
            entry.profile = Logger::GetInstance().Utf8ToWide(line.substr(0, first));
            entry.entryPoint =
                Logger::GetInstance().Utf8ToWide(line.substr(first + 1, second - first - 1));
            entry.filePath = NormalizeManifestPath(Logger::GetInstance().Utf8ToWide(line.substr(second + 1)));

            if (entry.profile.empty() || entry.filePath.empty()) {
                continue;
            }
            entries.push_back(std::move(entry));
        }

        // 綴りにそろえた結果、同じ行が重なることがある
        std::set<Entry> unique(entries.begin(), entries.end());
        return std::vector<Entry>(unique.begin(), unique.end());
    }

    std::vector<ShaderManifest::Entry> ShaderManifest::Collect() const
    {
        const std::vector<Entry> saved = Load();
        std::set<Entry> merged(saved.begin(), saved.end());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            merged.insert(recorded_.begin(), recorded_.end());
        }
        return std::vector<Entry>(merged.begin(), merged.end());
    }

    // 一覧を "profile|entryPoint|綴り" のテキストで書き出す
    void ShaderManifest::Save()
    {
        if (!enabled_) {
            return;
        }

        const std::vector<Entry> snapshot = Collect();
        if (snapshot.empty()) {
            return;
        }

        // 書き出す内容を先に組み立てる。既存と同じなら書かない
        std::ostringstream out;
        out << kHeaderLine1 << "\n" << kHeaderLine2 << "\n" << kHeaderLine3 << "\n";
        for (const Entry& entry : snapshot) {
            out << Logger::GetInstance().WideToUtf8(entry.profile) << "|"
                << Logger::GetInstance().WideToUtf8(entry.entryPoint) << "|"
                << Logger::GetInstance().WideToUtf8(entry.filePath) << "\n";
        }
        const std::string contents = out.str();

        {
            std::ifstream existing(path_, std::ios::binary);
            if (existing) {
                std::ostringstream buffer;
                buffer << existing.rdbuf();
                if (buffer.str() == contents) {
                    return;   // 内容が同じなら更新時刻を動かさない
                }
            }
        }

        std::ofstream file(path_, std::ios::binary | std::ios::trunc);
        if (!file) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Shader,
                "シェーダ一覧の保存に失敗しました: {}",
                Logger::GetInstance().PathToUtf8(path_));
            return;
        }
        file << contents;

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Shader,
            "[ShaderManifest] {} 件を記録しました（次回のコールド起動から並列に事前コンパイルされます）",
            snapshot.size());
    }
}
