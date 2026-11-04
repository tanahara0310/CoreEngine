#include "pch.h"
#include "ShaderGraphAsset.h"

#ifdef CORE_EDITOR

#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Material/ShaderMaterialLibrary.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace CoreEngine::Editor::ShaderGraph
{
    namespace
    {
        /// @brief 生成したシェーダの名前の印
        constexpr const wchar_t* kGeneratedSuffix = L".Graph.PS.hlsl";

        std::wstring Lower(std::wstring text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            return text;
        }

        bool EndsWith(const std::wstring& text, const std::wstring& suffix)
        {
            return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        std::string ReadText(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                return {};
            }
            std::stringstream text;
            text << in.rdbuf();
            return text.str();
        }

        /// @brief ファイルの先頭のコメントの行を読む（コメントでない行で止める）
        std::vector<std::string> ReadHeaderComments(const std::filesystem::path& file)
        {
            std::vector<std::string> lines;
            std::ifstream in(file, std::ios::binary);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.starts_with("//")) {
                    break;
                }
                lines.push_back(line);
            }
            return lines;
        }

        /// @brief 数の文字列を読む（後ろの f は読まない）
        bool ParseFloat(const std::string& text, float& value)
        {
            const char* const begin = text.data();
            return std::from_chars(begin, begin + text.size(), value).ec == std::errc();
        }
    }

    bool IsGraphFile(const std::filesystem::path& path)
    {
        const std::wstring name = Lower(path.filename().wstring());
        return EndsWith(name, L".shadergraph") || EndsWith(name, L".shadergraph.meta");
    }

    std::filesystem::path GeneratedShaderPath(const std::filesystem::path& graphFile)
    {
        return graphFile.parent_path() / (graphFile.stem().wstring() + kGeneratedSuffix);
    }

    bool IsGeneratedShaderPath(const std::filesystem::path& path)
    {
        return EndsWith(Lower(path.filename().wstring()), Lower(kGeneratedSuffix));
    }

    std::string ToAssetText(const std::filesystem::path& fullPath)
    {
        const std::filesystem::path relative = ProjectPaths::MakeRelative(fullPath);
        return Logger::GetInstance().PathToUtf8(relative.empty() ? fullPath : relative);
    }

    bool LoadGraphFile(const std::filesystem::path& file, Graph& graph, std::string& error,
        std::vector<std::string>& warnings)
    {
        const std::string text = ReadText(file);
        if (text.empty()) {
            error = "グラフのファイルを読めません";
            return false;
        }
        const json root = json::parse(text, nullptr, false);
        if (root.is_discarded()) {
            error = "グラフのファイルが JSON として読めません";
            return false;
        }
        if (!ReadGraph(root, graph, warnings)) {
            error = "グラフの形が違います";
            return false;
        }
        return true;
    }

    bool WriteFileReplacing(const std::filesystem::path& file, const std::string& text)
    {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        std::filesystem::path temporary = file;
        temporary += L".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << text;
            out.close();
            if (!out) {
                std::filesystem::remove(temporary, ec);
                return false;
            }
        }
        std::filesystem::rename(temporary, file, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            return false;
        }
        return true;
    }

    std::filesystem::path ScratchShaderPath(const std::string& key)
    {
        return ProjectPaths::Intermediate("ShaderGraph") / Logger::GetInstance().Utf8ToPath(key + ".PS.hlsl");
    }

    CompileCheck CheckCompile(ShaderCompiler& compiler, const std::string& hlsl, const std::filesystem::path& scratchFile)
    {
        CompileCheck check;
        if (!WriteFileReplacing(scratchFile, hlsl)) {
            check.message = "検査用のファイルを書けません: " + Logger::GetInstance().PathToUtf8(scratchFile);
            return check;
        }
        std::string error;
        std::string warning;
        const auto blob = compiler.CompileForCooking(scratchFile.wstring(), L"ps_6_0", L"main", error, &warning);
        if (!blob) {
            check.message = error;
            return check;
        }
        if (!warning.empty()) {
            check.message = "警告が出ました: " + warning;
            return check;
        }
        check.ok = true;
        return check;
    }

    std::vector<int> FindErrorLines(const std::string& message)
    {
        std::vector<int> lines;
        const std::string marker = ".PS.hlsl:";
        size_t position = 0;
        while ((position = message.find(marker, position)) != std::string::npos) {
            position += marker.size();
            int line = 0;
            size_t cursor = position;
            while (cursor < message.size() && std::isdigit(static_cast<unsigned char>(message[cursor]))) {
                line = line * 10 + (message[cursor] - '0');
                ++cursor;
            }
            if (cursor > position && line > 0) {
                lines.push_back(line);
            }
        }
        return lines;
    }

    SaveResult SaveGraph(const std::filesystem::path& graphFile, const Graph& graph, ShaderCompiler& compiler)
    {
        SaveResult result;
        if (!WriteFileReplacing(graphFile, JsonManager::ToFileText(WriteGraph(graph)))) {
            result.error = "グラフを書けません";
            return result;
        }
        result.graphSaved = true;
        AssetDatabase::GetInstance().ImportAsset(graphFile);

        GenerateOptions options;
        options.sourcePath = ToAssetText(graphFile);
        result.generated = Generate(graph, options);
        if (!result.generated.fatal.empty()) {
            result.error = result.generated.fatal;
            return result;
        }

        const std::filesystem::path shaderFile = GeneratedShaderPath(graphFile);
        if (ReadText(shaderFile) != result.generated.hlsl) {
            const std::string key = AssetDatabase::GetInstance().GetGUID(graphFile);
            const CompileCheck check = CheckCompile(compiler, result.generated.hlsl,
                ScratchShaderPath((key.empty() ? std::string("graph") : key) + ".save"));
            if (!check.ok) {
                result.error = check.message;
                return result;
            }
            if (!WriteFileReplacing(shaderFile, result.generated.hlsl)) {
                result.error = "シェーダを書けません";
                return result;
            }
            AssetDatabase::GetInstance().ImportAsset(shaderFile);
            result.shaderWritten = true;
        }
        result.updatedMaterials = SyncMaterials(graphFile, graph);
        return result;
    }

    bool CreateGraph(const std::filesystem::path& folder, const std::string& name, Target target,
        std::filesystem::path* created, std::string* error)
    {
        const auto fail = [error](std::string message) {
            if (error) {
                *error = std::move(message);
            }
            return false;
        };
        if (name.empty() || name.find_first_of("\\/:*?\"<>|.") != std::string::npos) {
            return fail("名前に使えない文字があります（\\ / : * ? \" < > | . は使えません）。");
        }
        Logger& log = Logger::GetInstance();
        const std::filesystem::path base = folder.is_absolute() ? folder : ProjectPaths::Resolve(log.PathToUtf8(folder));
        const std::filesystem::path graphFile = base / log.Utf8ToPath(name + kGraphExtension);
        const std::filesystem::path shaderFile = GeneratedShaderPath(graphFile);
        const std::filesystem::path materialFile = base / log.Utf8ToPath(name + ".mat");
        if (std::filesystem::exists(graphFile)) {
            return fail("同じ名前のグラフがあります。");
        }
        if (std::filesystem::exists(shaderFile)) {
            return fail("同じ名前のシェーダがあります。");
        }
        if (std::filesystem::exists(materialFile)) {
            return fail("同じ名前のマテリアルがあります。");
        }

        const Graph graph = MakeEmptyGraph(target);
        GenerateOptions options;
        options.sourcePath = ToAssetText(graphFile);
        const GenerateResult generated = Generate(graph, options);
        if (!generated.fatal.empty()) {
            return fail(generated.fatal);
        }
        if (!WriteFileReplacing(graphFile, JsonManager::ToFileText(WriteGraph(graph)))) {
            return fail("グラフを書けません。");
        }
        if (!WriteFileReplacing(shaderFile, generated.hlsl)) {
            return fail("シェーダを書けません。");
        }
        AssetDatabase::GetInstance().ImportAsset(graphFile);
        AssetDatabase::GetInstance().ImportAsset(shaderFile);

        json material = json::object();
        material["shader"] = AssetPathToJson(ToAssetText(shaderFile));
        material["values"] = json::object();
        material["textures"] = json::object();
        if (!WriteFileReplacing(materialFile, JsonManager::ToFileText(material))) {
            return fail("マテリアルを書けません。");
        }
        AssetDatabase::GetInstance().ImportAsset(materialFile);

        if (created) {
            *created = graphFile;
        }
        return true;
    }

    int SyncMaterials(const std::filesystem::path& graphFile, const Graph& graph)
    {
        const std::string shaderText = ToAssetText(GeneratedShaderPath(graphFile));
        int updated = 0;
        for (const AssetInfo* info : AssetDatabase::GetInstance().GetAssetsOfType(AssetType::Material)) {
            if (info->fullPath.extension() != ".mat") {
                continue;
            }
            std::string shader;
            std::string vertexShader;
            if (!ShaderMaterial::ReadShaderPaths(info->fullPath, shader, vertexShader) || shader != shaderText) {
                continue;
            }
            const std::shared_ptr<ShaderMaterial> material = ShaderMaterialLibrary::GetInstance().Get(ToAssetText(info->fullPath));
            if (!material) {
                continue;
            }
            bool changed = false;
            for (const Property& property : graph.properties) {
                if (property.type == PropertyType::Texture2D) {
                    if (!material->FindTexture(property.name) && !property.texture.empty()) {
                        material->SetTexture(property.name, MaterialTexture{ property.texture, property.linear });
                        changed = true;
                    }
                    continue;
                }
                const size_t width = static_cast<size_t>(WidthOf(property.type));
                const MaterialNumbers* const current = material->FindNumbers(property.name);
                if (current && current->size() == width) {
                    continue;
                }
                material->SetNumbers(property.name, std::span<const float>(property.value.data(), width));
                changed = true;
            }
            if (changed && material->Save()) {
                ++updated;
            }
        }
        return updated;
    }

    std::string ReadSourceGraph(const std::filesystem::path& shaderFile)
    {
        const std::vector<std::string> lines = ReadHeaderComments(shaderFile);
        if (lines.empty() || !lines.front().starts_with(kGeneratedMarker)) {
            return {};
        }
        std::string source = lines.front().substr(std::string(kGeneratedMarker).size());
        if (const size_t end = source.find(kGraphExtension); end != std::string::npos) {
            source.resize(end + std::string(kGraphExtension).size());
        }
        return source;
    }

    std::vector<PropertyHint> ReadPropertyHints(const std::filesystem::path& shaderFile)
    {
        std::vector<PropertyHint> hints;
        for (const std::string& line : ReadHeaderComments(shaderFile)) {
            if (!line.starts_with(kPropertyLinePrefix)) {
                continue;
            }
            std::istringstream words(line.substr(std::string(kPropertyLinePrefix).size()));
            PropertyHint hint;
            std::string type;
            words >> hint.name >> type;
            hint.color = type == "Color";
            std::string word;
            while (words >> word) {
                if (word != "range") {
                    continue;
                }
                std::string low;
                std::string high;
                words >> low >> high;
                hint.hasRange = ParseFloat(low, hint.rangeMin) && ParseFloat(high, hint.rangeMax)
                    && hint.rangeMin < hint.rangeMax;
            }
            if (!hint.name.empty()) {
                hints.push_back(std::move(hint));
            }
        }
        return hints;
    }
}

#endif // CORE_EDITOR
