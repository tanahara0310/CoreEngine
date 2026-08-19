#include "pch.h"
#include "ShaderManifest.h"

#include <fstream>
#include <sstream>

#include "Utility/Logger/Logger.h"

namespace
{
    constexpr char kHeaderLine1[] =
        "# CoreEngine shader manifest - 実行時に記録された「実際にコンパイルされるシェーダ」の一覧";
    constexpr char kHeaderLine2[] =
        "# 形式: <profile>|<entryPoint>|<解決前のパス>   entryPoint が空ならライブラリ(-E なし)";
    constexpr char kHeaderLine3[] =
        "# 自動生成。手で編集しても次回起動で上書きされる";
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

    void ShaderManifest::Record(const std::wstring& filePath,
        const wchar_t* profile,
        const wchar_t* entryPoint)
    {
        if (!enabled_) {
            return;
        }

        Entry entry;
        entry.filePath = filePath;
        entry.profile = profile ? profile : L"";
        entry.entryPoint = entryPoint ? entryPoint : L"";

        std::lock_guard<std::mutex> lock(mutex_);
        recorded_.insert(std::move(entry));
    }

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
            entry.filePath = Logger::GetInstance().Utf8ToWide(line.substr(second + 1));

            if (entry.profile.empty() || entry.filePath.empty()) {
                continue;
            }
            entries.push_back(std::move(entry));
        }

        return entries;
    }

    void ShaderManifest::Save()
    {
        if (!enabled_) {
            return;
        }

        std::set<Entry> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot = recorded_;
        }

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
