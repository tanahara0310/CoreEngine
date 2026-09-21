#include "pch.h"
#include "Editor/Script/ScriptTemplate.h"

#ifdef CORE_EDITOR

#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace CoreEngine::Editor::ScriptTemplate
{
    namespace
    {
        /// 雛形の置き場（プロジェクトの根からの相対パス）
        constexpr const char* kTemplateRoot = "Application/Config/ScriptTemplates";

        /// 雛形の拡張子。`.as` にするとスクリプト本体と一緒にコンパイルされてしまう
        constexpr const char* kTemplateExtension = ".txt";

        /// 雛形の中でクラス名に置き換わる目印
        constexpr const char* kClassPlaceholder = "{CLASS}";

        /// @brief 雛形の id から表示名を作る（分からなければ id をそのまま）
        std::string LabelOf(const std::string& id)
        {
            if (id == "Empty") { return "空"; }
            if (id == "Basic") { return "基本（Start と Update）"; }
            if (id == "Collision") { return "当たり判定つき"; }
            return id;
        }

        /// @brief 雛形が無いときに使う中身
        std::string FallbackText()
        {
            return "[DisplayName(\"{CLASS}\")]\r\n"
                   "class {CLASS} : ScriptComponent\r\n"
                   "{\r\n"
                   "}\r\n";
        }

        /// @brief すべての `{CLASS}` をクラス名に置き換える
        std::string Fill(std::string text, const std::string& className)
        {
            const std::string placeholder = kClassPlaceholder;
            for (std::size_t at = text.find(placeholder); at != std::string::npos;
                 at = text.find(placeholder, at + className.size())) {
                text.replace(at, placeholder.size(), className);
            }
            return text;
        }

        /// @brief 置き先がスクリプトのフォルダの中か
        bool IsInsideScriptRoot(const std::filesystem::path& relativeFolder)
        {
            const std::filesystem::path root = GetScriptRoot();
            const std::filesystem::path normalized = relativeFolder.lexically_normal();
            const std::filesystem::path fromRoot = normalized.lexically_relative(root);
            return !fromRoot.empty() && *fromRoot.begin() != "..";
        }
    }

    std::filesystem::path GetScriptRoot()
    {
        return std::filesystem::path("Application") / "Assets" / "Scripts";
    }

    std::vector<Entry> List()
    {
        std::vector<Entry> entries;
        std::error_code ec;
        const std::filesystem::path root = ProjectPaths::Resolve(kTemplateRoot);
        if (!std::filesystem::is_directory(root, ec)) {
            return entries;
        }

        for (const auto& item : std::filesystem::directory_iterator(root, ec)) {
            if (ec) { break; }
            if (!item.is_regular_file(ec) || item.path().extension() != kTemplateExtension) {
                continue;
            }
            // `Basic.as.txt` → `Basic`
            std::filesystem::path stem = item.path().stem();  // Basic.as
            if (stem.extension() == ".as") {
                stem = stem.stem();                           // Basic
            }
            const std::string id = stem.string();
            entries.push_back(Entry{ id, LabelOf(id) });
        }

        // 空 → 基本 → 当たり判定つき の順に出す（並びが実行ごとに変わらないように）
        static const std::vector<std::string> kOrder = { "Empty", "Basic", "Collision" };
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            const auto rank = [](const std::string& id) {
                const auto found = std::find(kOrder.begin(), kOrder.end(), id);
                return static_cast<int>(found - kOrder.begin());
                };
            const int ra = rank(a.id);
            const int rb = rank(b.id);
            return (ra != rb) ? (ra < rb) : (a.id < b.id);
            });
        return entries;
    }

    bool IsValidClassName(const std::string& name, std::string* outError)
    {
        const auto fail = [outError](const char* reason) {
            if (outError) { *outError = reason; }
            return false;
            };

        if (name.empty()) {
            return fail("名前を入れてください");
        }
        if (name.size() > 64) {
            return fail("名前が長すぎます（64 文字まで）");
        }
        const unsigned char first = static_cast<unsigned char>(name.front());
        if (!std::isalpha(first) && name.front() != '_') {
            return fail("英字か _ で始めてください");
        }
        for (const char c : name) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (!std::isalnum(u) && c != '_') {
                return fail("使えるのは英数字と _ だけです");
            }
        }
        if (outError) { outError->clear(); }
        return true;
    }

    bool Create(const std::filesystem::path& folder, const std::string& className,
                const std::string& templateId, std::filesystem::path* outPath,
                std::string* outError)
    {
        const auto fail = [outError](const std::string& reason) {
            if (outError) { *outError = reason; }
            return false;
            };

        if (!IsValidClassName(className, outError)) {
            return false;
        }
        if (!IsInsideScriptRoot(folder)) {
            return fail("置き先は " + GetScriptRoot().generic_string() + " の中にしてください");
        }

        const std::filesystem::path directory = ProjectPaths::Resolve(folder.generic_string());
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec) {
            return fail("フォルダを作れませんでした");
        }

        // ファイル名はクラス名と同じにする（外れているとコンパイルのときに警告が出る）
        const std::filesystem::path path = directory / (className + ".as");
        if (std::filesystem::exists(path, ec)) {
            return fail("同じ名前のスクリプトがもうあります");
        }

        std::string text = FallbackText();
        if (!templateId.empty()) {
            const std::filesystem::path templatePath =
                ProjectPaths::Resolve(kTemplateRoot) / (templateId + ".as" + kTemplateExtension);
            std::ifstream in(templatePath, std::ios::binary);
            if (in) {
                std::ostringstream buffer;
                buffer << in.rdbuf();
                text = buffer.str();
            } else {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                    "スクリプトの雛形を読めないので中身の無いクラスを作ります: {}",
                    Logger::GetInstance().PathToUtf8(templatePath));
            }
        }

        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return fail("ファイルを作れませんでした");
        }
        const std::string filled = Fill(std::move(text), className);
        out.write(filled.data(), static_cast<std::streamsize>(filled.size()));
        out.close();

        if (outPath) { *outPath = path; }
        if (outError) { outError->clear(); }
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Script,
            "スクリプトを作りました: {}", Logger::GetInstance().PathToUtf8(path));
        return true;
    }
}

#endif // CORE_EDITOR
