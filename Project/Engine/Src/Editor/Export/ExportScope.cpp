#include "pch.h"
#include "Editor/Export/ExportScope.h"

#ifdef CORE_EDITOR

#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Model/Derived/ModelDerivedData.h"
#include "Graphics/Texture/Path/TexturePathResolver.h"
#include "Threading/ThreadPool.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <cwctype>
#include <fstream>
#include <future>
#include <map>
#include <sstream>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief パスとして引く文字列の長さの上限
        constexpr size_t kMaxPathLength = 259;

        /// @brief パスを照合用のキーにする（正規化して小文字）
        std::wstring MakeFileKey(const std::filesystem::path& path)
        {
            std::wstring key = path.lexically_normal().generic_wstring();
            for (wchar_t& c : key) {
                c = static_cast<wchar_t>(std::towlower(c));
            }
            return key;
        }

        /// @brief 書き出さずに済ませられる種類か（ほかのファイルを指さない、重いアセット）
        bool IsExcludable(AssetType type)
        {
            return type == AssetType::Model || type == AssetType::Texture || type == AssetType::Audio;
        }

        /// @brief 拡張子を小文字で返す
        std::wstring LowerExtension(const std::filesystem::path& path)
        {
            return MakeFileKey(path.extension());
        }

        /// @brief 中身が JSON で、アセットを指しうるファイルか
        bool IsJsonFile(const std::filesystem::path& path)
        {
            const std::wstring extension = LowerExtension(path);
            return extension == L".json" || extension == L".prefab" || extension == L".mat" ||
                extension == L".physmat" || extension == L".waterprofile";
        }

        /// @brief スクリプトの文字列リテラルを集める（コメントの中は除く）
        void CollectLiterals(const std::string& source, std::set<std::string>& out)
        {
            const size_t n = source.size();
            size_t i = 0;
            while (i < n) {
                const char c = source[i];
                if (c == '/' && i + 1 < n && source[i + 1] == '/') {
                    i = source.find('\n', i);
                    if (i == std::string::npos) {
                        break;
                    }
                    continue;
                }
                if (c == '/' && i + 1 < n && source[i + 1] == '*') {
                    i = source.find("*/", i + 2);
                    if (i == std::string::npos) {
                        break;
                    }
                    i += 2;
                    continue;
                }
                if (source.compare(i, 3, "\"\"\"") == 0) {
                    const size_t end = source.find("\"\"\"", i + 3);
                    if (end == std::string::npos) {
                        break;
                    }
                    out.insert(source.substr(i + 3, end - i - 3));
                    i = end + 3;
                    continue;
                }
                if (c == '"' || c == '\'') {
                    std::string text;
                    ++i;
                    while (i < n && source[i] != c && source[i] != '\n') {
                        if (source[i] == '\\' && i + 1 < n) {
                            text += source[i + 1];
                            i += 2;
                            continue;
                        }
                        text += source[i++];
                    }
                    ++i;
                    if (!text.empty()) {
                        out.insert(std::move(text));
                    }
                    continue;
                }
                ++i;
            }
        }

        /// @brief CSV のセルを文字列として集める
        void CollectCsvCells(const std::string& source, std::set<std::string>& out)
        {
            std::string cell;
            for (const char c : source) {
                if (c == ',' || c == '\n' || c == '\r' || c == '\t') {
                    if (!cell.empty()) {
                        out.insert(cell);
                        cell.clear();
                    }
                } else if (c != '"') {
                    cell += c;
                }
            }
            if (!cell.empty()) {
                out.insert(cell);
            }
        }

        /// @brief ファイルを丸ごと読む（読めなければ空）
        std::string ReadText(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            std::ostringstream text;
            text << file.rdbuf();
            return text.str();
        }

        /// @brief ファイルから、そのファイルが指すアセットをたどる
        class ReferenceWalker
        {
        public:
            /// @param loose ファイル名だけの文字列も引く（false なら `{"guid", "path"}` と `/` を含むパスだけ引く）
            explicit ReferenceWalker(bool loose) : loose_(loose) {}

            /// @brief ファイル 1 つの中から、指すアセットを集める（JSON・スクリプト・CSV のほかは見ない）
            void AddFile(const std::filesystem::path& file)
            {
                const std::wstring extension = LowerExtension(file);
                if (IsJsonFile(file)) {
                    const json root = JsonManager::ParseFile(file);
                    if (!root.is_discarded()) {
                        Walk(root);
                    }
                    return;
                }
                std::set<std::string> strings;
                if (extension == L".as") {
                    CollectLiterals(ReadText(file), strings);
                } else if (extension == L".csv") {
                    CollectCsvCells(ReadText(file), strings);
                }
                for (const std::string& text : strings) {
                    AddString(text);
                }
            }

            /// @brief たどり着いた、書き出さずに済ませられるアセット（キーと情報）
            const std::map<std::wstring, const AssetInfo*>& GetReached() const { return reached_; }

            /// @brief たどり着いたモデルが使うテクスチャを足す
            /// @param textures モデルのキーから、そのモデルのマテリアルが使うテクスチャ
            void AddModelTextures(const std::map<std::wstring, std::vector<const AssetInfo*>>& textures)
            {
                std::vector<std::wstring> models;
                for (const auto& [key, info] : reached_) {
                    if (info->type == AssetType::Model) {
                        models.push_back(key);
                    }
                }
                for (const std::wstring& model : models) {
                    if (const auto found = textures.find(model); found != textures.end()) {
                        for (const AssetInfo* texture : found->second) {
                            AddAsset(texture);
                        }
                    }
                }
            }

        private:
            void Walk(const json& node)
            {
                if (node.is_object()) {
                    const auto guid = node.find("guid");
                    const auto path = node.find("path");
                    const bool hasGuid = guid != node.end() && guid->is_string();
                    const bool hasPath = path != node.end() && path->is_string();
                    if (hasGuid || hasPath) {
                        Reflection::AssetRefValue value;
                        value.guid = hasGuid ? guid->get<std::string>() : std::string();
                        value.path = hasPath ? path->get<std::string>() : std::string();
                        if (const AssetInfo* info = ResolveAssetRef(value)) {
                            AddAsset(info);
                            return;
                        }
                    }
                    for (const auto& item : node.items()) {
                        Walk(item.value());
                    }
                } else if (node.is_array()) {
                    for (const json& item : node) {
                        Walk(item);
                    }
                } else if (node.is_string()) {
                    AddString(node.get_ref<const std::string&>());
                }
            }

            void AddString(const std::string& text)
            {
                if (text.empty() || text.size() > kMaxPathLength) {
                    return;
                }
                if (!loose_ && text.find_first_of("/\\") == std::string::npos) {
                    return;
                }
                AddAsset(FindAssetInfo(text));
            }

            void AddAsset(const AssetInfo* info)
            {
                if (info && IsExcludable(info->type)) {
                    reached_.try_emplace(MakeFileKey(info->fullPath), info);
                }
            }

            bool loose_ = false;
            std::map<std::wstring, const AssetInfo*> reached_;
        };

        /// @brief モデルのマテリアルが使うテクスチャを、モデルごとに並べて読む（解き方は実行時と同じ）
        std::map<std::wstring, std::vector<const AssetInfo*>> LoadModelTextures(
            const std::vector<const AssetInfo*>& models, ThreadPool& pool)
        {
            std::vector<std::vector<const AssetInfo*>> textures(models.size());
            std::vector<std::future<void>> futures;
            futures.reserve(models.size());
            for (size_t i = 0; i < models.size(); ++i) {
                futures.push_back(pool.Submit("ExportScope", [&models, &textures, i] {
                    MeshAsset asset;
                    if (!ModelDerivedData::Load(models[i]->fullPath, asset)) {
                        return;
                    }
                    const TexturePathResolver resolver;
                    AssetDatabase& database = AssetDatabase::GetInstance();
                    Logger& log = Logger::GetInstance();
                    for (const MaterialAsset& material : asset.model.materials) {
                        for (const std::string* texture : { &material.baseColorTexture, &material.emissiveTexture,
                                 &material.metallicRoughnessTexture, &material.normalTexture, &material.occlusionTexture }) {
                            if (texture->empty()) {
                                continue;
                            }
                            const std::filesystem::path resolved = resolver.ResolveAssetPath(*texture);
                            if (const AssetInfo* info = database.FindAssetByPath(log.PathToUtf8(resolved))) {
                                textures[i].push_back(info);
                            }
                        }
                    }
                }));
            }
            for (std::future<void>& future : futures) {
                pool.Wait(future);
            }

            std::map<std::wstring, std::vector<const AssetInfo*>> result;
            for (size_t i = 0; i < models.size(); ++i) {
                result.emplace(MakeFileKey(models[i]->fullPath), std::move(textures[i]));
            }
            return result;
        }

        /// @brief フォルダの下のファイルを全部挙げる（skip のフォルダの中は見ない）
        std::vector<std::filesystem::path> ListFiles(const std::filesystem::path& folder,
            const std::set<std::wstring>& skip = {})
        {
            std::vector<std::filesystem::path> files;
            std::error_code ec;
            std::filesystem::recursive_directory_iterator it(folder, ec);
            for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (it->is_directory(ec)) {
                    if (skip.contains(MakeFileKey(it->path()))) {
                        it.disable_recursion_pending();
                    }
                    continue;
                }
                if (it->is_regular_file(ec)) {
                    files.push_back(it->path());
                }
            }
            return files;
        }

        /// @brief エディタだけで読むスクリプトのフォルダ
        std::filesystem::path EditorScriptsFolder()
        {
            return ProjectPaths::ProjectRoot() / "Application" / "Assets" / "Scripts" / "Editor";
        }
    }

    ExportScope ExportScope::Build(const std::vector<std::string>& excludedScenes, ThreadPool& pool)
    {
        ExportScope scope;
        if (excludedScenes.empty()) {
            return scope;
        }
        Logger& log = Logger::GetInstance();
        AssetDatabase& database = AssetDatabase::GetInstance();
        const std::filesystem::path assets = ProjectPaths::ProjectRoot() / "Application" / "Assets";

        // 外すシーンのフォルダのファイルは書き出さず、そこから指す先を候補にする
        std::set<std::wstring> skipFolders{ MakeFileKey(EditorScriptsFolder()) };
        ReferenceWalker candidates(false);
        for (const std::string& scene : excludedScenes) {
            const std::filesystem::path folder = assets / "Scenes" / log.Utf8ToPath(scene);
            std::error_code ec;
            if (scene.empty() || !std::filesystem::is_directory(folder, ec)) {
                continue;
            }
            skipFolders.insert(MakeFileKey(folder));
            for (const std::filesystem::path& file : ListFiles(folder)) {
                scope.excludedFiles_.insert(MakeFileKey(file));
                candidates.AddFile(file);
            }
        }

        // 書き出すもの（外すシーンと、書き出さずに済ませられるアセットと .meta のほか全部）から指す先は残す
        ReferenceWalker kept(true);
        for (const std::filesystem::path& file : ListFiles(assets, skipFolders)) {
            if (LowerExtension(file) == L".meta") {
                continue;
            }
            const AssetInfo* info = database.FindAssetByPath(log.PathToUtf8(file));
            if (info && IsExcludable(info->type)) {
                continue;
            }
            kept.AddFile(file);
        }
        for (const std::filesystem::path& file : ListFiles(ProjectPaths::ProjectRoot() / "Application" / "Config")) {
            kept.AddFile(file);
        }

        // 候補と残す側のモデルが使うテクスチャも、それぞれの側へ足す
        std::map<std::wstring, const AssetInfo*> models;
        for (const ReferenceWalker* walker : { &candidates, &kept }) {
            for (const auto& [key, info] : walker->GetReached()) {
                if (info->type == AssetType::Model) {
                    models.try_emplace(key, info);
                }
            }
        }
        std::vector<const AssetInfo*> modelList;
        modelList.reserve(models.size());
        for (const auto& [key, info] : models) {
            modelList.push_back(info);
        }
        const std::map<std::wstring, std::vector<const AssetInfo*>> textures = LoadModelTextures(modelList, pool);
        candidates.AddModelTextures(textures);
        kept.AddModelTextures(textures);

        // 候補のうち、残す側がたどり着かない Application のアセットを書き出さない
        const std::wstring applicationKey = MakeFileKey(assets) + L"/";
        for (const auto& [key, info] : candidates.GetReached()) {
            if (kept.GetReached().contains(key) || !key.starts_with(applicationKey)) {
                continue;
            }
            scope.excludedFiles_.insert(key);
            scope.excludedFiles_.insert(key + L".meta");
            switch (info->type) {
            case AssetType::Model:   ++scope.excludedModels_; break;
            case AssetType::Texture: ++scope.excludedTextures_; break;
            case AssetType::Audio:   ++scope.excludedAudio_; break;
            default: break;
            }
        }
        return scope;
    }

    bool ExportScope::Includes(const std::filesystem::path& file) const
    {
        return excludedFiles_.empty() || !excludedFiles_.contains(MakeFileKey(file));
    }

    std::set<std::string> ExportScope::CollectScriptStrings()
    {
        std::set<std::string> strings;
        const std::set<std::wstring> skip{ MakeFileKey(EditorScriptsFolder()) };
        for (const std::filesystem::path& file : ListFiles(ProjectPaths::ProjectRoot() / "Application" / "Assets", skip)) {
            if (LowerExtension(file) == L".as") {
                CollectLiterals(ReadText(file), strings);
            }
        }
        return strings;
    }
}

#endif // CORE_EDITOR
