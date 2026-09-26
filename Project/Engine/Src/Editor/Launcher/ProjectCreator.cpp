#include "pch.h"
#include "Editor/Launcher/ProjectCreator.h"

#ifdef CORE_EDITOR

#include "Utility/Path/ProjectPaths.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <string_view>

namespace CoreEngine::Editor
{
    namespace
    {
        /// 名前の長さの上限
        constexpr std::size_t kMaxNameLength = 64;

        /// Windows がフォルダ名に使わせない名前
        constexpr std::array<std::string_view, 22> kReservedNames = {
            "CON", "PRN", "AUX", "NUL",
            "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
        };

        /// 新しいプロジェクトの .gitignore
        constexpr const char* kGitIgnore =
            "# 自分だけの状態（エディタの配置・セーブデータ）\n"
            "Application/Saved/\n"
            "# 作り直せるもの（ログ・テクスチャのキャッシュ）\n"
            "Intermediate/\n"
            "# 書き出したゲーム\n"
            "Build/\n";

        /// 新しいプロジェクトの VS Code のワークスペース
        constexpr const char* kWorkspace =
            "{\n"
            "\t\"folders\": [\n"
            "\t\t{\n"
            "\t\t\t\"name\": \"Scripts\",\n"
            "\t\t\t\"path\": \"Application/Assets/Scripts\"\n"
            "\t\t}\n"
            "\t],\n"
            "\t\"settings\": {\n"
            "\t\t\"angelScript.implicitMutualInclusion\": true\n"
            "\t},\n"
            "\t\"extensions\": {\n"
            "\t\t\"recommendations\": [\n"
            "\t\t\t\"sashi0034.angel-lsp\"\n"
            "\t\t]\n"
            "\t}\n"
            "}\n";

        /// @brief path を表示用の UTF-8 にする
        std::string ToUtf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        /// @brief UTF-8 の文字列を path にする
        std::filesystem::path FromUtf8(const std::string& text)
        {
            return std::filesystem::path(std::u8string(text.begin(), text.end()));
        }

        /// @brief JSON のオブジェクトの文字列の値（無ければ空）
        std::string StringOf(const nlohmann::json& object, const char* key)
        {
            const auto found = object.find(key);
            return (found != object.end() && found->is_string()) ? found->get<std::string>() : std::string{};
        }

        /// @brief 比べるための形（区切りをそろえ、末尾の区切りを落とし、小文字にする）
        std::wstring ComparableText(const std::filesystem::path& path)
        {
            std::wstring text = path.lexically_normal().native();
            while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/')) {
                text.pop_back();
            }
            std::transform(text.begin(), text.end(), text.begin(),
                [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            return text;
        }

        /// @brief path が base そのものか、その下にあるか（大文字小文字は区別しない）
        bool IsSameOrUnder(const std::filesystem::path& path, const std::filesystem::path& base)
        {
            const std::wstring p = ComparableText(path);
            const std::wstring b = ComparableText(base);
            if (p == b) {
                return true;
            }
            return p.size() > b.size() && p.compare(0, b.size(), b) == 0 && p[b.size()] == L'\\';
        }

        /// @brief ASCII 以外の文字を含むか
        bool HasNonAscii(const std::wstring& text)
        {
            return std::any_of(text.begin(), text.end(), [](wchar_t c) { return c > 0x7F; });
        }

        /// @brief テキストを CRLF で書く
        bool WriteText(const std::filesystem::path& path, std::string_view text)
        {
            std::string crlf;
            crlf.reserve(text.size() + 32);
            for (const char c : text) {
                if (c == '\n') {
                    crlf += '\r';
                }
                crlf += c;
            }
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) {
                return false;
            }
            out.write(crlf.data(), static_cast<std::streamsize>(crlf.size()));
            return static_cast<bool>(out);
        }
    }

    std::vector<ProjectTemplate> ProjectCreator::ListTemplates()
    {
        std::vector<ProjectTemplate> templates;
        std::error_code ec;
        const std::filesystem::path root = ProjectPaths::Resolve("Engine/Templates/Projects");
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (!entry.is_directory(ec)) {
                continue;
            }
            std::ifstream in(entry.path() / "Template.json", std::ios::binary);
            if (!in) {
                continue;
            }
            const nlohmann::json json = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
            if (json.is_discarded() || !json.is_object()) {
                continue;
            }

            ProjectTemplate projectTemplate;
            projectTemplate.id = ToUtf8(entry.path().filename());
            projectTemplate.name = StringOf(json, "name");
            if (projectTemplate.name.empty()) {
                projectTemplate.name = projectTemplate.id;
            }
            projectTemplate.description = StringOf(json, "description");
            projectTemplate.initialScene = StringOf(json, "initialScene");
            if (const auto order = json.find("order"); order != json.end() && order->is_number_integer()) {
                projectTemplate.order = order->get<int>();
            }
            projectTemplate.folder = entry.path();

            const std::filesystem::path assets = entry.path() / "Application" / "Assets";
            for (const auto& scene : std::filesystem::directory_iterator(assets / "Scenes", ec)) {
                if (scene.is_directory(ec) && std::filesystem::is_regular_file(scene.path() / "_scene.json", ec)) {
                    projectTemplate.scenes.push_back(ToUtf8(scene.path().filename()));
                }
            }
            std::filesystem::recursive_directory_iterator it(assets / "Scripts", ec);
            for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (it->is_regular_file(ec) && it->path().extension() == ".as") {
                    projectTemplate.scripts.push_back(ToUtf8(it->path().filename()));
                }
            }
            ec.clear();
            templates.push_back(std::move(projectTemplate));
        }

        std::sort(templates.begin(), templates.end(), [](const ProjectTemplate& a, const ProjectTemplate& b) {
            return (a.order != b.order) ? (a.order < b.order) : (a.name < b.name);
            });
        return templates;
    }

    std::string ProjectCreator::Check(const std::string& name, const std::filesystem::path& location,
                                      std::string* outWarning)
    {
        if (outWarning) {
            outWarning->clear();
        }

        if (name.empty()) {
            return "名前を入れてください";
        }
        if (name.size() > kMaxNameLength) {
            return "名前が長すぎます（64 文字まで）";
        }
        for (const char c : name) {
            const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                 c == '_' || c == '-';
            if (!allowed) {
                return "名前はそのままフォルダ名になるので、英数字と _ - だけにしてください";
            }
        }
        std::string upper = name;
        std::transform(upper.begin(), upper.end(), upper.begin(),
            [](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; });
        if (std::find(kReservedNames.begin(), kReservedNames.end(), upper) != kReservedNames.end()) {
            return "Windows が予約している名前です";
        }

        if (location.empty()) {
            return "保存先を入れてください";
        }
        if (!location.is_absolute()) {
            return "保存先はドライブ名から書いてください（例: C:\\CoreEngine\\Projects）";
        }
        std::error_code ec;
        if (!std::filesystem::is_directory(location, ec)) {
            return "保存先のフォルダがありません";
        }
        if (IsSameOrUnder(location, ProjectPaths::EngineRoot())) {
            return "エンジンのフォルダ（" + ToUtf8(ProjectPaths::EngineRoot()) + "）の中には作れません";
        }
        const std::filesystem::path folder = location / FromUtf8(name);
        if (std::filesystem::exists(folder, ec)) {
            return ToUtf8(folder) + " はもうあります。別の名前にしてください";
        }

        if (outWarning && HasNonAscii(location.native())) {
            *outWarning = "保存先に英数字以外の文字が入っています。日本語などを含むパスはうまく扱えないことがあるので、"
                          "英数字だけの場所をおすすめします";
        }
        return {};
    }

    bool ProjectCreator::Create(const ProjectTemplate& projectTemplate, const std::string& name,
                                const std::filesystem::path& location, std::filesystem::path* outFolder,
                                std::string* outError)
    {
        const auto fail = [outError](std::string reason) {
            if (outError) { *outError = std::move(reason); }
            return false;
            };

        if (std::string reason = Check(name, location, nullptr); !reason.empty()) {
            return fail(std::move(reason));
        }

        const std::filesystem::path folder = location / FromUtf8(name);
        std::error_code ec;
        if (!std::filesystem::create_directory(folder, ec) || ec) {
            return fail("フォルダを作れませんでした");
        }

        // ここから先で失敗したら、作ったフォルダを消す
        const auto rollback = [&folder, &fail](std::string reason) {
            std::error_code removeError;
            std::filesystem::remove_all(folder, removeError);
            return fail(std::move(reason));
            };

        std::filesystem::copy(projectTemplate.folder / "Application", folder / "Application",
            std::filesystem::copy_options::recursive, ec);
        if (ec) {
            return rollback("テンプレートを写せませんでした");
        }

        const std::filesystem::path settings = folder / "Application" / "Config" / "EngineSettings";
        std::filesystem::create_directories(settings, ec);
        if (ec) {
            return rollback("設定のフォルダを作れませんでした");
        }

        nlohmann::json project = nlohmann::json::object();
        project["name"] = name;
        project["initialScene"] = projectTemplate.initialScene;
        project["version"] = "1.0";
        if (!WriteText(settings / "Project.json", project.dump(4) + "\n")) {
            return rollback("Project.json を書けませんでした");
        }
        if (!WriteText(folder / ".gitignore", kGitIgnore)) {
            return rollback(".gitignore を書けませんでした");
        }
        if (!WriteText(folder / FromUtf8(name + ".code-workspace"), kWorkspace)) {
            return rollback("VS Code のワークスペースを書けませんでした");
        }

        if (outFolder) {
            *outFolder = folder;
        }
        if (outError) {
            outError->clear();
        }
        return true;
    }
}

#endif // CORE_EDITOR
