#include "pch.h"
#include "ShaderGraphAsset.h"

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraphSubGraph.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Material/ShaderMaterialLibrary.h"
#include "Graphics/Material/SurfaceMaterialPipeline.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cwctype>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <system_error>

namespace CoreEngine::Editor::ShaderGraph
{
    namespace
    {
        /// @brief 生成したシェーダの名前の印（スプライト・ポストエフェクト・3D）
        constexpr const wchar_t* kGeneratedSuffix = L".Graph.PS.hlsl";
        constexpr const wchar_t* kGeneratedComputeSuffix = L".Graph.CS.hlsl";
        constexpr const wchar_t* kGeneratedSurfaceSuffix = L".Graph.Surface.hlsl";

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

        /// @brief 読んだサブグラフ（ファイルの更新日時が変わるまで使う）
        struct CachedSubGraph
        {
            std::filesystem::file_time_type time;
            std::shared_ptr<const Graph> graph;
        };

        /// @brief サブグラフのファイルを読む（同じ更新日時なら前に読んだものを返す）
        std::shared_ptr<const Graph> LoadSubGraph(const std::filesystem::path& file, std::string& error)
        {
            static std::mutex mutex;
            static std::map<std::wstring, CachedSubGraph> cache;
            std::error_code ec;
            const std::filesystem::file_time_type time = std::filesystem::last_write_time(file, ec);
            if (ec) {
                error = "サブグラフ " + Logger::GetInstance().PathToUtf8(file.filename()) + " が見つかりません";
                return nullptr;
            }
            const std::wstring key = Lower(file.lexically_normal().wstring());
            {
                std::scoped_lock lock(mutex);
                if (const auto found = cache.find(key); found != cache.end() && found->second.time == time) {
                    return found->second.graph;
                }
            }
            auto graph = std::make_shared<Graph>();
            std::vector<std::string> warnings;
            if (!LoadGraphFile(file, *graph, error, warnings)) {
                return nullptr;
            }
            if (!graph->subGraph) {
                error = Logger::GetInstance().PathToUtf8(file.filename()) + " はサブグラフではありません";
                return nullptr;
            }
            std::scoped_lock lock(mutex);
            cache[key] = CachedSubGraph{ time, graph };
            return graph;
        }

        /// @brief 生成したシェーダを、前と違えば検査コンパイルを通してから書く
        /// @return 書けなければ訳（書いた・前と同じなら空）
        std::string WriteGenerated(const std::filesystem::path& graphFile, Target target, const std::string& hlsl,
            ShaderCompiler& compiler, bool& written)
        {
            written = false;
            const std::filesystem::path shaderFile = GeneratedShaderPath(graphFile, target);
            if (ReadText(shaderFile) == hlsl) {
                return {};
            }
            const std::string key = AssetDatabase::GetInstance().GetGUID(graphFile);
            const CompileCheck check = CheckCompile(compiler, hlsl,
                ScratchShaderPath((key.empty() ? std::string("graph") : key) + ".save", target), target, true);
            if (!check.ok) {
                return check.message;
            }
            if (!WriteFileReplacing(shaderFile, hlsl)) {
                return "シェーダを書けません";
            }
            AssetDatabase::GetInstance().ImportAsset(shaderFile);
            written = true;
            return {};
        }
    }

    bool IsGraphFile(const std::filesystem::path& path)
    {
        const std::wstring name = Lower(path.filename().wstring());
        return EndsWith(name, L".shadergraph") || EndsWith(name, L".shadergraph.meta")
            || EndsWith(name, L".shadersubgraph") || EndsWith(name, L".shadersubgraph.meta");
    }

    bool IsSubGraphFile(const std::filesystem::path& path)
    {
        return EndsWith(Lower(path.filename().wstring()), L".shadersubgraph");
    }

    std::filesystem::path FindSubGraphFile(const std::string& guid, const std::string& path)
    {
        std::filesystem::path file;
        if (!guid.empty()) {
            if (const AssetInfo* const info = AssetDatabase::GetInstance().FindAssetByGUID(guid)) {
                file = info->fullPath;
            }
        }
        if (file.empty() && !path.empty()) {
            file = ProjectPaths::Resolve(path);
        }
        std::error_code ec;
        if (file.empty() || !std::filesystem::exists(file, ec)) {
            return {};
        }
        return file;
    }

    SubGraphResolver MakeSubGraphResolver()
    {
        return [](const std::string& guid, const std::string& path, std::string& error) -> std::shared_ptr<const Graph> {
            const std::filesystem::path file = FindSubGraphFile(guid, path);
            if (file.empty()) {
                error = "サブグラフ " + path + " が見つかりません";
                return nullptr;
            }
            return LoadSubGraph(file, error);
        };
    }

    std::filesystem::path GeneratedShaderPath(const std::filesystem::path& graphFile, Target target)
    {
        const wchar_t* suffix = kGeneratedSuffix;
        if (target == Target::PostEffect) {
            suffix = kGeneratedComputeSuffix;
        } else if (Is3D(target)) {
            suffix = kGeneratedSurfaceSuffix;
        }
        return graphFile.parent_path() / (graphFile.stem().wstring() + suffix);
    }

    bool IsGeneratedShaderPath(const std::filesystem::path& path)
    {
        const std::wstring name = Lower(path.filename().wstring());
        return EndsWith(name, Lower(kGeneratedSuffix)) || EndsWith(name, Lower(kGeneratedComputeSuffix))
            || EndsWith(name, Lower(kGeneratedSurfaceSuffix));
    }

    bool IsComputeShaderPath(const std::filesystem::path& path)
    {
        return EndsWith(Lower(path.filename().wstring()), L".cs.hlsl");
    }

    const wchar_t* ShaderProfile(Target target)
    {
        return target == Target::PostEffect ? L"cs_6_0" : L"ps_6_0";
    }

    IncludeResolver MakeIncludeResolver()
    {
        return [](const std::string& file, std::string& includeName, std::string& error) {
            Logger& log = Logger::GetInstance();
            const std::filesystem::path path = log.Utf8ToPath(file);
            const std::wstring name = Lower(path.filename().wstring());
            if (!EndsWith(name, L".hlsl") && !EndsWith(name, L".hlsli")) {
                error = "関数のファイルは .hlsl か .hlsli にしてください";
                return false;
            }
            const std::filesystem::path full = ProjectPaths::Resolve(file);
            std::error_code ec;
            if (!std::filesystem::exists(full, ec)) {
                error = "関数のファイル " + file + " がありません";
                return false;
            }
            int same = 0;
            for (const AssetInfo* info : AssetDatabase::GetInstance().GetAssetsOfType(AssetType::Shader)) {
                if (Lower(info->fullPath.filename().wstring()) == name) {
                    ++same;
                }
            }
            if (same > 1) {
                error = "同じ名前のシェーダのファイル（" + log.PathToUtf8(path.filename())
                    + "）がほかのフォルダにもあります。名前を変えてください";
                return false;
            }
            includeName = log.PathToUtf8(path.filename());
            return true;
        };
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

    std::filesystem::path ScratchShaderPath(const std::string& key, Target target)
    {
        const char* suffix = ".PS.hlsl";
        if (target == Target::PostEffect) {
            suffix = ".CS.hlsl";
        } else if (Is3D(target)) {
            suffix = ".Surface.hlsl";
        }
        return ProjectPaths::Intermediate("ShaderGraph") / Logger::GetInstance().Utf8ToPath(key + suffix);
    }

    CompileCheck CheckCompile(ShaderCompiler& compiler, const std::string& hlsl, const std::filesystem::path& scratchFile,
        Target target, bool allEntries)
    {
        CompileCheck check;
        if (!WriteFileReplacing(scratchFile, hlsl)) {
            check.message = "検査用のファイルを書けません: " + Logger::GetInstance().PathToUtf8(scratchFile);
            return check;
        }

        struct Entry
        {
            const wchar_t* profile;
            const wchar_t* name;
        };
        std::vector<Entry> entries;
        if (!Is3D(target)) {
            entries.push_back({ ShaderProfile(target), L"main" });
        } else {
            entries.push_back({ L"ps_6_0", kSurfacePreviewPixelEntry });
            entries.push_back({ L"vs_6_0", kSurfacePreviewVertexEntry });
            if (allEntries) {
                for (const SurfaceMaterialStage& stage : kSurfaceMaterialStages) {
                    const bool listed = std::any_of(entries.begin(), entries.end(),
                        [&stage](const Entry& entry) { return std::wstring_view(entry.name) == stage.entryPoint; });
                    if (!listed) {
                        entries.push_back({ L"ps_6_0", stage.entryPoint });
                    }
                }
                if (SurfaceShaderTextHasOwnVertexShader(hlsl)) {
                    entries.push_back({ L"vs_6_0", kSurfaceStaticVertexEntry });
                    entries.push_back({ L"vs_6_0", kSurfaceSkinnedVertexEntry });
                }
                if (SurfaceShaderTextHasVertexFunction(hlsl)) {
                    entries.push_back({ L"cs_6_0", kSurfaceDeformEntry });
                }
            }
        }

        for (const Entry& entry : entries) {
            std::string label;
            if (Is3D(target)) {
                for (const wchar_t* c = entry.name; *c; ++c) {
                    label += static_cast<char>(*c);
                }
                label = "入口 " + label + ": ";
            }
            std::string error;
            std::string warning;
            const auto blob = compiler.CompileForCooking(scratchFile.wstring(), entry.profile, entry.name, error, &warning);
            if (!blob) {
                check.message = label + error;
                return check;
            }
            if (!warning.empty()) {
                check.message = label + "警告が出ました: " + warning;
                return check;
            }
        }
        check.ok = true;
        return check;
    }

    std::vector<int> FindErrorLines(const std::string& message, const std::filesystem::path& file)
    {
        std::vector<int> lines;
        const std::string marker = Logger::GetInstance().PathToUtf8(file.filename()) + ":";
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
        options.resolveInclude = MakeIncludeResolver();
        options.resolveSubGraph = MakeSubGraphResolver();
        result.generated = Generate(graph, options);
        if (!result.generated.fatal.empty()) {
            result.error = result.generated.fatal;
            return result;
        }

        result.error = WriteGenerated(graphFile, graph.target, result.generated.hlsl, compiler, result.shaderWritten);
        if (!result.error.empty()) {
            return result;
        }
        result.updatedMaterials = SyncMaterials(graphFile, graph);
        return result;
    }

    bool CreateSubGraph(const std::filesystem::path& folder, const std::string& name, Target previewTarget,
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
        const std::filesystem::path file = base / log.Utf8ToPath(name + kSubGraphExtension);
        if (std::filesystem::exists(file)) {
            return fail("同じ名前のサブグラフがあります。");
        }
        if (!WriteFileReplacing(file, JsonManager::ToFileText(WriteGraph(MakeEmptySubGraph(previewTarget))))) {
            return fail("サブグラフを書けません。");
        }
        AssetDatabase::GetInstance().ImportAsset(file);
        if (created) {
            *created = file;
        }
        return true;
    }

    bool SaveSubGraph(const std::filesystem::path& file, const Graph& graph, std::string& error)
    {
        if (!WriteFileReplacing(file, JsonManager::ToFileText(WriteGraph(graph)))) {
            error = "サブグラフを書けません";
            return false;
        }
        AssetDatabase::GetInstance().ImportAsset(file);
        return true;
    }

    std::vector<std::filesystem::path> FindGraphFiles()
    {
        std::vector<std::filesystem::path> files;
        for (const AssetInfo* info : AssetDatabase::GetInstance().GetAssetsOfType(AssetType::ShaderGraph)) {
            if (EndsWith(Lower(info->fullPath.filename().wstring()), L".shadergraph")) {
                files.push_back(info->fullPath);
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    Regeneration PrepareRegeneration(const std::filesystem::path& graphFile, const std::string& guid, const std::string& path,
        ShaderCompiler& compiler, const SubGraphResolver& resolve)
    {
        Regeneration regeneration;
        regeneration.file = graphFile;
        std::error_code ec;
        regeneration.time = std::filesystem::last_write_time(graphFile, ec);
        std::string error;
        std::vector<std::string> warnings;
        if (ec || !LoadGraphFile(graphFile, regeneration.graph, error, warnings)
            || !UsesSubGraph(regeneration.graph, guid, path, resolve)) {
            regeneration.graph = Graph{};
            return regeneration;
        }
        regeneration.uses = true;
        SyncSubGraphNodes(regeneration.graph, resolve);

        GenerateOptions options;
        options.sourcePath = ToAssetText(graphFile);
        options.resolveInclude = MakeIncludeResolver();
        options.resolveSubGraph = resolve;
        const GenerateResult generated = Generate(regeneration.graph, options);
        if (!generated.fatal.empty()) {
            regeneration.error = generated.fatal;
            return regeneration;
        }
        const Target target = regeneration.graph.target;
        if (ReadText(GeneratedShaderPath(graphFile, target)) == generated.hlsl) {
            return regeneration;
        }
        const std::string key = AssetDatabase::GetInstance().GetGUID(graphFile);
        const CompileCheck check = CheckCompile(compiler, generated.hlsl,
            ScratchShaderPath((key.empty() ? std::string("graph") : key) + ".regenerate", target), target, true);
        if (!check.ok) {
            regeneration.error = check.message;
            return regeneration;
        }
        regeneration.hlsl = generated.hlsl;
        return regeneration;
    }

    bool ApplyRegeneration(Regeneration& regeneration)
    {
        if (!regeneration.uses || !regeneration.error.empty()) {
            return regeneration.error.empty();
        }
        // 作っている間にグラフのファイルが書き換わっていれば、何も書かない
        std::error_code ec;
        if (std::filesystem::last_write_time(regeneration.file, ec) != regeneration.time || ec) {
            return true;
        }
        if (!regeneration.hlsl.empty()) {
            const std::filesystem::path shaderFile = GeneratedShaderPath(regeneration.file, regeneration.graph.target);
            if (!WriteFileReplacing(shaderFile, regeneration.hlsl)) {
                regeneration.error = "シェーダを書けません";
                return false;
            }
            AssetDatabase::GetInstance().ImportAsset(shaderFile);
        }
        SyncMaterials(regeneration.file, regeneration.graph);
        return true;
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
        const std::filesystem::path shaderFile = GeneratedShaderPath(graphFile, target);
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

    std::vector<std::filesystem::path> FindGraphMaterials(const std::filesystem::path& graphFile, Target target)
    {
        const std::string shaderText = ToAssetText(GeneratedShaderPath(graphFile, target));
        std::vector<std::filesystem::path> materials;
        for (const AssetInfo* info : AssetDatabase::GetInstance().GetAssetsOfType(AssetType::Material)) {
            if (info->fullPath.extension() != ".mat") {
                continue;
            }
            std::string shader;
            std::string vertexShader;
            if (ShaderMaterial::ReadShaderPaths(info->fullPath, shader, vertexShader) && shader == shaderText) {
                materials.push_back(info->fullPath);
            }
        }
        std::sort(materials.begin(), materials.end());
        return materials;
    }

    int SyncMaterials(const std::filesystem::path& graphFile, const Graph& graph)
    {
        int updated = 0;
        for (const std::filesystem::path& file : FindGraphMaterials(graphFile, graph.target)) {
            const std::shared_ptr<ShaderMaterial> material = ShaderMaterialLibrary::GetInstance().Get(ToAssetText(file));
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
