#include "pch.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "SceneSaveSystem.h"
#include "GameObject/Component/Core/ObjectRef.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetRef.h"
#include "Reflection/PropertySerializer.h"
#include "Reflection/PropertyValue.h"
#include "Reflection/TypeDescriptor.h"
#include "Graphics/Light/Light.h"
#include "Scene/Feature/LightingFeature.h"
#include "Scene/PrefabSystem.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <functional>
#include <filesystem>

namespace CoreEngine
{
    namespace
    {
        // ──────────────────────────────────────────────────────────
        // シーンフォルダのスキーマ（_scene.json の "objects" 配列 → <key>.json）を
        // 読む処理はここに集約する。Load と CollectModelPaths が別々に解析していると、
        // スキーマ変更で先読みだけが静かに空振りする（遅くなるだけで気づけない）。
        // ──────────────────────────────────────────────────────────

        /// @brief シーンの保存データを置くフォルダ
        constexpr const char* kScenesRoot = "Application/Assets/Scenes";

        std::string MakeSceneDir(const std::string& sceneName) {
            return std::string(kScenesRoot) + "/" + sceneName;
        }

        /// @brief シーンマニフェスト（_scene.json）のパス
        std::string MakeManifestPath(const std::string& sceneName) {
            return MakeSceneDir(sceneName) + "/_scene.json";
        }

        /// @brief オブジェクト個別ファイルのパス
        std::string MakeObjectPath(const std::string& sceneName, const std::string& key) {
            return MakeSceneDir(sceneName) + "/" + key + ".json";
        }

        /// @brief マニフェストに列挙された各オブジェクトの JSON を順に訪問する
        /// @param sceneName シーン名
        /// @param visitor   (キー, 読み込んだ JSON) を受け取る。読めなかった項目は来ない
        void ForEachManifestObject(
            const std::string& sceneName,
            const std::function<void(const std::string& key, const json& data)>& visitor)
        {
            auto& jm = JsonManager::GetInstance();

            const std::string manifestPath = MakeManifestPath(sceneName);
            if (!jm.FileExists(manifestPath)) {
                return;
            }

            json manifest = jm.LoadJson(manifestPath);
            if (!manifest.contains("objects") || !manifest["objects"].is_array()) {
                return;
            }

            for (const auto& entry : manifest["objects"]) {
                if (!entry.is_string()) continue;

                const std::string key = entry.get<std::string>();
                if (key.empty()) continue;

                const std::string objPath = MakeObjectPath(sceneName, key);
                if (!jm.FileExists(objPath)) continue;

                json data = jm.LoadJson(objPath);
                if (data.is_null()) continue;

                visitor(key, data);
            }
        }

        /// @brief 保存された ID をオブジェクトへ戻す
        void RestoreObjectId(GameObjectManager& mgr, GameObject& object, const json& data)
        {
            if (!data.contains("id") || !data.at("id").is_string()) {
                return;
            }

            const std::string text = data.at("id").get<std::string>();
            const ObjectId id = ObjectId::FromString(text);
            if (!id.IsValid()) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                    "SceneSaveSystem: \"{}\" の id \"{}\" を読めません",
                    object.GetSerializeKey(), text);
                return;
            }

            if (!mgr.AssignObjectId(object, id)) {
                const GameObject* other = mgr.FindObject(id);
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                    "SceneSaveSystem: \"{}\" の id {} は \"{}\" が使っているので戻せません",
                    object.GetSerializeKey(), text,
                    other ? other->GetSerializeKey() : std::string{});
            }
        }

        /// @brief AssetRef の指す先を引き、見つからない・GUID とパスが食い違う・種類が違うものを警告する
        void WarnAssetRef(const std::string& sceneName, const GameObject& object,
                          const IComponent& component, const Reflection::PropertyDescriptor& p,
                          const Reflection::AssetRefValue& ref)
        {
            if (ref.guid.empty() && ref.path.empty()) {
                return;
            }

            Logger& log = Logger::GetInstance();
            const AssetInfo* byGuid =
                ref.guid.empty() ? nullptr : AssetDatabase::GetInstance().FindAssetByGUID(ref.guid);
            const AssetInfo* byPath = FindAssetInfo(ref.path);
            const AssetInfo* target = byGuid ? byGuid : byPath;

            if (!target) {
                log.Logf(LogLevel::Warn, LogCategory::Resource,
                    "SceneSaveSystem: \"{}\" の {} / {} / {} が指すアセット（GUID \"{}\" / パス \"{}\"）が見つかりません",
                    sceneName, object.GetSerializeKey(), component.GetTypeName(), p.name, ref.guid, ref.path);
                return;
            }

            if (!ref.guid.empty() && !byGuid) {
                log.Logf(LogLevel::Warn, LogCategory::Resource,
                    "SceneSaveSystem: \"{}\" の {} / {} / {} の GUID \"{}\" が見つからないので、パス \"{}\" で引きました",
                    sceneName, object.GetSerializeKey(), component.GetTypeName(), p.name, ref.guid, ref.path);
            } else if (byGuid && !ref.path.empty() && byPath != byGuid) {
                log.Logf(LogLevel::Warn, LogCategory::Resource,
                    "SceneSaveSystem: \"{}\" の {} / {} / {} のパス \"{}\" は GUID の指す \"{}\" と食い違っています（GUID を使います）",
                    sceneName, object.GetSerializeKey(), component.GetTypeName(), p.name,
                    ref.path, ToAssetPath(*byGuid));
            }

            if (target->type != p.assetType) {
                log.Logf(LogLevel::Warn, LogCategory::Resource,
                    "SceneSaveSystem: \"{}\" の {} / {} / {} が指す \"{}\" は {} ではなく {} です",
                    sceneName, object.GetSerializeKey(), component.GetTypeName(), p.name,
                    ToAssetPath(*target), AssetTypeToString(p.assetType), AssetTypeToString(target->type));
            }
        }

        /// @brief 復元したオブジェクトの参照（ObjectRef / AssetRef）のうち、指す先が見つからないものを警告する
        void WarnUnresolvedReferences(const GameObjectManager& mgr,
                                      const std::vector<const GameObject*>& objects,
                                      const std::string& sceneName)
        {
            Reflection::PropertyValue value;
            for (const GameObject* object : objects) {
                if (!object) continue;

                for (const auto& slot : object->GetAllComponents()) {
                    IComponent* component = slot.get();
                    const Reflection::TypeDescriptor* descriptor =
                        component ? component->GetTypeDescriptor() : nullptr;
                    if (!descriptor) continue;

                    for (const auto& p : descriptor->properties) {
                        if (!p.IsValid()) continue;

                        if (p.type == Reflection::PropertyType::AssetRef) {
                            value.LoadFrom(p, component->GetReflectionInstance());
                            if (const auto* asset =
                                    static_cast<const Reflection::AssetRefValue*>(value.Data(p.type))) {
                                WarnAssetRef(sceneName, *object, *component, p, *asset);
                            }
                            continue;
                        }
                        if (p.type != Reflection::PropertyType::ObjectRef) continue;

                        value.LoadFrom(p, component->GetReflectionInstance());
                        const auto* ref =
                            static_cast<const Reflection::ObjectRefValue*>(value.Data(p.type));
                        if (!ref || !ref->objectId.IsValid()) continue;

                        const GameObject* target = mgr.FindObject(ref->objectId);
                        if (target &&
                            FindReferencedComponent(*target, p, ref->componentType)) {
                            continue;
                        }

                        Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                            "SceneSaveSystem: \"{}\" の {} / {} / {} が指す {}（{}）が見つかりません",
                            sceneName, object->GetSerializeKey(), component->GetTypeName(), p.name,
                            ref->objectId.ToString(), ref->componentType);
                    }
                }
            }
        }

        /// @brief オブジェクト 1 体の保存 JSON を作る（プレハブから作ったものは差分の形にする）
        json BuildObjectJson(const GameObject& object)
        {
            json data = object.Serialize();
            if (data.empty()) {
                return data;
            }

            data["id"] = object.GetObjectId().ToString();

            if (object.IsPrefabInstance()) {
                const Reflection::AssetRefValue prefab = object.GetPrefab().GetValue();
                if (const json* components = PrefabSystem::LoadComponents(prefab)) {
                    data = PrefabSystem::MakeInstanceJson(data, *components);
                } else {
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                        "SceneSaveSystem: \"{}\" のプレハブ（GUID \"{}\" / パス \"{}\"）を読めないので、構成をそのまま保存します",
                        object.GetSerializeKey(), prefab.guid, prefab.path);
                }
                data["prefab"] = Reflection::PropertySerializer::AssetRefToJson(prefab);
            }
            return data;
        }

        /// @brief プレハブの値に保存された差分を重ねて、オブジェクトへ戻す
        void RestorePrefabInstance(const std::string& sceneName, GameObject& object, const json& data)
        {
            const Reflection::AssetRefValue prefab = PrefabSystem::ReadPrefabRef(data);
            const json* components = PrefabSystem::LoadComponents(prefab);
            if (!components && !data.contains("components")) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                    "SceneSaveSystem: \"{}\" の {} のプレハブ（GUID \"{}\" / パス \"{}\"）が見つからないので、足したコンポーネントだけで組みます",
                    sceneName, object.GetSerializeKey(), prefab.guid, prefab.path);
            }

            std::vector<std::string> problems;
            object.SetPrefab(prefab);
            object.Deserialize(PrefabSystem::ExpandInstanceJson(data, components, &problems));
            for (const std::string& problem : problems) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                    "SceneSaveSystem: \"{}\" の {}: {}", sceneName, object.GetSerializeKey(), problem);
            }
        }

        /// @brief components 配列からモデルを指す AssetRef を探し、そのパスを重ならないように足す
        void CollectModelRefs(const json& components, std::vector<std::string>& modelPaths)
        {
            if (!components.is_array()) {
                return;
            }

            for (const auto& entry : components) {
                if (!entry.is_object() || !entry.contains("parameters") ||
                    !entry.at("parameters").is_object()) {
                    continue;
                }
                for (const auto& node : entry.at("parameters")) {
                    Reflection::AssetRefValue ref;
                    if (!node.is_object() || !Reflection::PropertySerializer::JsonToAssetRef(node, ref)) {
                        continue;
                    }
                    const AssetInfo* info = ResolveAssetRef(ref);
                    if (!info || info->type != AssetType::Model) {
                        continue;
                    }
                    std::string path = ToAssetPath(*info);
                    if (std::find(modelPaths.begin(), modelPaths.end(), path) == modelPaths.end()) {
                        modelPaths.push_back(std::move(path));
                    }
                }
            }
        }

        /// @brief オブジェクト 1 体の保存 JSON からモデルを指す参照を探し、そのパスを重ならないように足す
        void CollectObjectModelRefs(const json& data, std::vector<std::string>& modelPaths)
        {
            // プレハブから作るものはプレハブの値を重ねてから探す
            if (data.contains("prefab")) {
                const json expanded = PrefabSystem::ExpandInstanceJson(
                    data, PrefabSystem::LoadComponents(PrefabSystem::ReadPrefabRef(data)));
                if (expanded.contains("components")) {
                    CollectModelRefs(expanded.at("components"), modelPaths);
                }
            } else if (data.contains("components")) {
                CollectModelRefs(data.at("components"), modelPaths);
            }
        }

        /// @brief 保存するオブジェクトを、保存 JSON と一緒に並びどおりに訪問する
        /// @param visitor (保存キー, 保存 JSON) を受け取る
        /// @note 保存しないもの・キーの無いもの・削除の印が付いたもの・書く値の無いものは来ない。
        void ForEachSavedObject(const GameObjectManager& mgr,
                                const std::function<void(const std::string& key, json data)>& visitor)
        {
            for (const auto& obj : mgr.GetAllObjects()) {
                if (!obj || obj->IsMarkedForDestroy() || !obj->IsSerializeEnabled()) continue;
                const std::string& key = obj->GetSerializeKey();
                if (key.empty()) continue;

                json data = BuildObjectJson(*obj);
                if (!data.empty()) {
                    visitor(key, std::move(data));
                }
            }
        }

        /// @brief キーを「, 」でつなぐ
        std::string JoinKeys(const std::vector<std::string>& keys)
        {
            std::string text;
            for (const std::string& key : keys) {
                if (!text.empty()) {
                    text += ", ";
                }
                text += key;
            }
            return text;
        }

        /// @brief シーンフォルダにある、指定したキーに含まれないオブジェクト JSON のキーを集める
        /// @param keys マニフェストに載っているキー
        /// @return 綴り順のキー（名前が `_` で始まるファイルとフォルダは含めない）
        std::vector<std::string> CollectOrphanObjectKeys(const std::string& sceneName,
                                                         const std::unordered_set<std::string>& keys)
        {
            namespace fs = std::filesystem;
            std::vector<std::string> orphans;

            std::error_code ec;
            const fs::path dir = ProjectPaths::Resolve(MakeSceneDir(sceneName));
            if (!fs::is_directory(dir, ec)) {
                return orphans;
            }

            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (!entry.is_regular_file(ec)) continue;

                const fs::path& file = entry.path();
                if (file.extension() != ".json") continue;

                const std::string stem = Logger::GetInstance().PathToUtf8(file.stem());
                if (stem.empty() || stem.front() == '_') continue;

                if (keys.find(stem) == keys.end()) {
                    orphans.push_back(stem);
                }
            }

            std::sort(orphans.begin(), orphans.end());
            return orphans;
        }

        /// @brief マニフェストに載っていないオブジェクト JSON を、エラーとして名前を挙げる
        void ReportOrphanObjectFiles(const std::string& sceneName)
        {
            auto& jm = JsonManager::GetInstance();

            const std::string manifestPath = MakeManifestPath(sceneName);
            if (!jm.FileExists(manifestPath)) {
                return;
            }

            json manifest = jm.LoadJson(manifestPath);
            if (!manifest.contains("objects") || !manifest["objects"].is_array()) {
                return;
            }

            std::unordered_set<std::string> keys;
            for (const auto& entry : manifest["objects"]) {
                if (entry.is_string()) {
                    keys.insert(entry.get<std::string>());
                }
            }

            const std::vector<std::string> orphans = CollectOrphanObjectKeys(sceneName, keys);
            if (orphans.empty()) {
                return;
            }

            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                "SceneSaveSystem: \"{}\" にマニフェストに無いオブジェクトの JSON が {} 件あります"
                "（読み込まれません）: {}",
                sceneName, orphans.size(), JoinKeys(orphans));
        }

        /// @brief マニフェストに載せたキー以外のオブジェクト JSON を消す
        void RemoveOrphanObjectFiles(const std::string& sceneName, const std::unordered_set<std::string>& keys)
        {
            const std::vector<std::string> orphans = CollectOrphanObjectKeys(sceneName, keys);
            if (orphans.empty()) {
                return;
            }

            std::vector<std::string> removed;
            for (const std::string& key : orphans) {
                std::error_code ec;
                if (std::filesystem::remove(ProjectPaths::Resolve(MakeObjectPath(sceneName, key)), ec)) {
                    removed.push_back(key);
                    continue;
                }
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                    "SceneSaveSystem: \"{}\" の {}.json を消せませんでした（エラー {}）",
                    sceneName, key, ec.value());
            }

            if (!removed.empty()) {
                Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Resource,
                    "SceneSaveSystem: \"{}\" のマニフェストに無いオブジェクトの JSON を {} 件消しました: {}",
                    sceneName, removed.size(), JoinKeys(removed));
            }
        }
    }

    // ===== パスヘルパー =====

    std::string SceneSaveSystem::GetSceneDir() const {
        return MakeSceneDir(sceneName_);
    }

    std::string SceneSaveSystem::GetManifestPath() const {
        return MakeManifestPath(sceneName_);
    }

    std::string SceneSaveSystem::GetObjectPath(const std::string& key) const {
        return MakeObjectPath(sceneName_, key);
    }

    // ===== 先読み用のパス列挙 =====

    std::vector<std::string> SceneSaveSystem::CollectModelPaths(const std::string& sceneName)
    {
        std::vector<std::string> modelPaths;
        if (sceneName.empty()) {
            return modelPaths;
        }

        ForEachManifestObject(sceneName, [&modelPaths](const std::string&, const json& data) {
            CollectObjectModelRefs(data, modelPaths);
        });

        return modelPaths;
    }

    std::vector<std::string> SceneSaveSystem::CollectModelPaths(const SceneSnapshot& snapshot)
    {
        std::vector<std::string> modelPaths;
        for (const SceneSnapshot::Object& object : snapshot.objects) {
            CollectObjectModelRefs(object.data, modelPaths);
        }
        return modelPaths;
    }

    // ===== 控え =====

    std::shared_ptr<const SceneSnapshot> SceneSaveSystem::CaptureSnapshot(const GameObjectManager& mgr)
    {
        auto snapshot = std::make_shared<SceneSnapshot>();
        ForEachSavedObject(mgr, [&snapshot](const std::string& key, json data) {
            snapshot->objects.push_back(SceneSnapshot::Object{ key, std::move(data) });
        });
        return snapshot;
    }

    // ===== Load =====

    void SceneSaveSystem::Load(GameObjectManager* mgr)
    {
        BeginLoad(mgr);
        while (!StepLoad()) {
        }
    }

    float SceneSaveSystem::GetLoadProgress() const
    {
        if (pendingObjects_.empty()) {
            return 1.0f;
        }
        return static_cast<float>(loadIndex_) / static_cast<float>(pendingObjects_.size());
    }

    bool SceneSaveSystem::StepLoad()
    {
        if (loadIndex_ < pendingObjects_.size()) {
            const PendingObject& pending = pendingObjects_[loadIndex_++];
            json loaded;
            const json* data = pending.data;
            if (!data) {
                loaded = JsonManager::GetInstance().LoadJson(pending.path);
                data = &loaded;
            }
            if (!data->is_null() && pending.object) {
                if (loadManager_) {
                    RestoreObjectId(*loadManager_, *pending.object, *data);
                }
                if (data->contains("prefab")) {
                    RestorePrefabInstance(sceneName_, *pending.object, *data);
                } else {
                    pending.object->Deserialize(*data);
                }
            }
            if (loadIndex_ < pendingObjects_.size()) {
                return false;
            }
        }

        // 全員を復元し終えたら、指す先が見つからない参照を挙げる
        if (loadManager_) {
            std::vector<const GameObject*> restored;
            restored.reserve(pendingObjects_.size());
            for (const PendingObject& pending : pendingObjects_) {
                restored.push_back(pending.object);
            }
            WarnUnresolvedReferences(*loadManager_, restored, sceneName_);
            loadManager_ = nullptr;
        }
        pendingObjects_.clear();
        loadIndex_ = 0;
        restoreSnapshot_.reset();
        return true;
    }

    std::vector<std::string> SceneSaveSystem::ListSavedScenes()
    {
        namespace fs = std::filesystem;
        std::vector<std::string> names;

        std::error_code ec;
        const fs::path root = ProjectPaths::Resolve(kScenesRoot);
        if (!fs::is_directory(root, ec)) {
            return names;
        }
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            if (!entry.is_directory(ec) || !fs::is_regular_file(entry.path() / "_scene.json", ec)) {
                continue;
            }
            const std::string name = Logger::GetInstance().PathToUtf8(entry.path().filename());
            if (!name.empty()) {
                names.push_back(name);
            }
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    bool SceneSaveSystem::IsValidSceneName(const std::string& name, std::string* error)
    {
        const auto fail = [error](const char* reason) {
            if (error) {
                *error = reason;
            }
            return false;
        };

        if (name.empty()) {
            return fail("名前を入れてください");
        }
        if (name.front() == '_') {
            return fail("`_` で始まる名前は使えません（保存データの予約）");
        }
        if (name.find_first_of("\\/:*?\"<>|") != std::string::npos) {
            return fail("\\ / : * ? \" < > | は使えません");
        }
        if (JsonManager::GetInstance().FileExists(MakeManifestPath(name))) {
            return fail("同じ名前のシーンが既にあります");
        }
        if (error) {
            error->clear();
        }
        return true;
    }

    bool SceneSaveSystem::CreateScene(const std::string& sceneName, SceneTemplate templateKind,
                                      std::string* error)
    {
        if (!IsValidSceneName(sceneName, error)) {
            return false;
        }

        auto& jm = JsonManager::GetInstance();
        if (!jm.CreateJsonDirectory(MakeSceneDir(sceneName))) {
            if (error) {
                *error = "フォルダを作れませんでした";
            }
            return false;
        }

        json manifest = json::object();
        manifest["objects"] = json::array();

        if (templateKind == SceneTemplate::Basic) {
            // 太陽をシーンのオブジェクトとして置く（値は LightingFeature の既定と同じ）
            const json sun = {
                { "active", true },
                { "name", "Sun" },
                { "components", json::array({
                    json{
                        { "type", "Transform" },
                        { "enabled", true },
                        { "parameters", json{
                            { "translate", json::array({ 0.0f, 5.0f, 0.0f }) },
                            { "rotate", json::array({ 0.0f, 0.0f, 0.0f }) },
                            { "scale", json::array({ 1.0f, 1.0f, 1.0f }) },
                        } },
                    },
                    json{
                        { "type", "Light" },
                        { "enabled", true },
                        { "parameters", json{
                            { "type", 0 },
                            { "color", json::array({ 1.0f, 1.0f, 1.0f, 1.0f }) },
                            { "intensity", LightUnits::kSunIlluminanceLux },
                            { "direction", json::array({ -0.45073172f, -0.65011942f, 0.61170721f }) },
                            { "isAtmosphereSun", true },
                            { "atmosphereIntensity", LightingFeature::kDefaultSunAtmosphereIntensity },
                        } },
                    },
                }) },
            };
            if (!jm.SaveJson(MakeObjectPath(sceneName, "Sun"), sun)) {
                if (error) {
                    *error = "太陽のファイルを書けませんでした";
                }
                return false;
            }
            manifest["objects"].push_back("Sun");

            // ゲームの視点をシーンのオブジェクトとして置く（構図は Transform が持つ）
            const json camera = {
                { "active", true },
                { "name", "MainCamera" },
                { "components", json::array({
                    json{
                        { "type", "Transform" },
                        { "enabled", true },
                        { "parameters", json{
                            { "translate", json::array({ 0.0f, 3.0f, -30.0f }) },
                            { "rotate", json::array({ 0.0f, 0.0f, 0.0f }) },
                            { "scale", json::array({ 1.0f, 1.0f, 1.0f }) },
                        } },
                    },
                    json{
                        { "type", "Camera" },
                        { "enabled", true },
                        { "parameters", json{
                            { "isMainCamera", true },
                            { "projection", 0 },
                            { "fov", 45.0f },
                            { "nearClip", 0.1f },
                            { "farClip", 1000.0f },
                        } },
                    },
                }) },
            };
            if (!jm.SaveJson(MakeObjectPath(sceneName, "MainCamera"), camera)) {
                if (error) {
                    *error = "カメラのファイルを書けませんでした";
                }
                return false;
            }
            manifest["objects"].push_back("MainCamera");

            manifest["defaultGround"] = true;
        }

        if (!jm.SaveJson(MakeManifestPath(sceneName), manifest)) {
            if (error) {
                *error = "マニフェストを書けませんでした";
            }
            return false;
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Resource,
            "シーン {} を作りました", sceneName);
        return true;
    }

    SceneSaveSystem::ManifestSettings SceneSaveSystem::LoadManifestSettings(const std::string& sceneName)
    {
        ManifestSettings settings;
        auto& jm = JsonManager::GetInstance();
        const std::string manifestPath = MakeManifestPath(sceneName);
        if (!jm.FileExists(manifestPath)) {
            return settings;
        }

        const json manifest = jm.LoadJson(manifestPath);
        if (!manifest.is_object()) {
            return settings;
        }
        if (const auto features = manifest.find("features"); features != manifest.end() && features->is_array()) {
            for (const auto& entry : *features) {
                if (entry.is_string() && !entry.get<std::string>().empty()) {
                    settings.features.push_back(entry.get<std::string>());
                }
            }
        }
        if (const auto ground = manifest.find("defaultGround"); ground != manifest.end() && ground->is_boolean()) {
            settings.defaultGround = ground->get<bool>();
        }
        if (const auto collision = manifest.find("collision");
            collision != manifest.end() && collision->is_object()) {
            const auto pairs = collision->find("pairs");
            if (pairs != collision->end() && pairs->is_array()) {
                std::vector<std::pair<std::string, std::string>> parsed;
                for (const auto& entry : *pairs) {
                    if (!entry.is_array() || entry.size() != 2 ||
                        !entry[0].is_string() || !entry[1].is_string()) {
                        continue;
                    }
                    parsed.emplace_back(entry[0].get<std::string>(), entry[1].get<std::string>());
                }
                settings.collisionPairs = std::move(parsed);
            }
        }
        return settings;
    }

    void SceneSaveSystem::SaveManifestSettings(const ManifestSettings& settings)
    {
        if (sceneName_.empty()) {
            return;
        }

        auto& jm = JsonManager::GetInstance();
        jm.CreateJsonDirectory(GetSceneDir());

        // オブジェクトの一覧と、ここで扱わない項目はそのまま残す
        json manifest = jm.FileExists(GetManifestPath()) ? jm.LoadJson(GetManifestPath()) : json::object();
        if (!manifest.is_object()) {
            manifest = json::object();
        }
        if (!manifest.contains("objects") || !manifest["objects"].is_array()) {
            manifest["objects"] = json::array();
        }

        if (settings.features.empty()) {
            manifest.erase("features");
        } else {
            manifest["features"] = settings.features;
        }

        if (settings.defaultGround) {
            manifest["defaultGround"] = *settings.defaultGround;
        } else {
            manifest.erase("defaultGround");
        }

        if (settings.collisionPairs) {
            json pairs = json::array();
            for (const auto& [first, second] : *settings.collisionPairs) {
                pairs.push_back(json::array({ first, second }));
            }
            manifest["collision"] = json{ { "pairs", std::move(pairs) } };
        } else {
            manifest.erase("collision");
        }

        jm.SaveJson(GetManifestPath(), manifest);
    }

    void SceneSaveSystem::BeginLoad(GameObjectManager* mgr)
    {
        pendingObjects_.clear();
        loadIndex_ = 0;
        loadManager_ = mgr;

        if (!mgr || (sceneName_.empty() && !restoreSnapshot_)) return;

        auto& jm = JsonManager::GetInstance();

        auto findObjectBySerializeKey = [mgr](const std::string& key) -> GameObject* {
            for (const auto& obj : mgr->GetAllObjects()) {
                if (obj && obj->GetSerializeKey() == key) {
                    return obj.get();
                }
            }
            return nullptr;
        };

        const auto placeDataObject =
            [mgr, &findObjectBySerializeKey](const std::string& key, const json& data) {
                // 既にシーン側が同じキーで生成済みならマニフェストからは作らない
                if (findObjectBySerializeKey(key)) {
                    return;
                }

                // プレハブから作るものと、コンポーネントの構成を持つものは空のオブジェクトだけを置き、
                // 構成は復元時に組む
                if (data.contains("prefab") ||
                    (data.contains("components") && data["components"].is_array())) {
                    auto obj = std::make_unique<GameObject>();
                    obj->SetName(key);
                    mgr->AddObject(std::move(obj));
                }
            };

        // 控えから読むときは、キーから控えの値を引けるようにする
        std::unordered_map<std::string, const json*> snapshotData;
        if (restoreSnapshot_) {
            for (const SceneSnapshot::Object& entry : restoreSnapshot_->objects) {
                snapshotData.emplace(entry.key, &entry.data);
                placeDataObject(entry.key, entry.data);
            }
        } else {
            ReportOrphanObjectFiles(sceneName_);
            ForEachManifestObject(sceneName_, placeDataObject);
        }

        // 復元対象を確定させる（実際のデシリアライズは StepLoad が 1 体ずつ行う）
        for (const auto& obj : mgr->GetAllObjects()) {
            if (!obj || !obj->IsSerializeEnabled()) continue;
            const std::string& key = obj->GetSerializeKey();
            if (key.empty()) continue;

            if (restoreSnapshot_) {
                const auto found = snapshotData.find(key);
                if (found == snapshotData.end()) continue;

                pendingObjects_.push_back(PendingObject{ obj.get(), std::string{}, found->second });
                continue;
            }

            std::string objPath = GetObjectPath(key);
            if (!jm.FileExists(objPath)) continue;

            pendingObjects_.push_back(PendingObject{ obj.get(), std::move(objPath) });
        }
    }

    // ===== SaveScene =====

    void SceneSaveSystem::SaveScene(GameObjectManager* mgr)
    {
        if (sceneName_.empty() || !mgr) return;

        auto& jm = JsonManager::GetInstance();
        jm.CreateJsonDirectory(GetSceneDir());

        // マニフェスト（オブジェクトキー一覧を書き直し、ほかの項目は読み込んだまま残す）
        json manifest = jm.FileExists(GetManifestPath()) ? jm.LoadJson(GetManifestPath()) : json::object();
        if (!manifest.is_object()) {
            manifest = json::object();
        }
        manifest["objects"] = json::array();
        std::unordered_set<std::string> savedKeys;

        // 各オブジェクトを個別ファイルに保存
        ForEachSavedObject(*mgr, [this, &jm, &manifest, &savedKeys](const std::string& key, json data) {
            jm.SaveJson(GetObjectPath(key), data);
            manifest["objects"].push_back(key);
            savedKeys.insert(key);
        });

        // マニフェストを保存し、載らなかったオブジェクトの JSON を消す
        if (jm.SaveJson(GetManifestPath(), manifest)) {
            RemoveOrphanObjectFiles(sceneName_, savedKeys);
        }

        if (onSaveNotification_) {
            onSaveNotification_("シーンを保存しました: " + sceneName_);
        }
    }

    // ===== SaveObject =====

    void SceneSaveSystem::SaveObject(GameObject* obj)
    {
        if (sceneName_.empty() || !obj || !obj->IsSerializeEnabled()) return;
        const std::string& key = obj->GetSerializeKey();
        if (key.empty()) return;

        auto& jm = JsonManager::GetInstance();
        jm.CreateJsonDirectory(GetSceneDir());

        // オブジェクトデータを個別ファイルに保存
        const json data = BuildObjectJson(*obj);
        if (!data.empty()) {
            jm.SaveJson(GetObjectPath(key), data);
        }

        // マニフェストにキーが含まれていなければ追加
        std::string manifestPath = GetManifestPath();
        json manifest;
        if (jm.FileExists(manifestPath)) {
            manifest = jm.LoadJson(manifestPath);
        }
        if (!manifest.contains("objects") || !manifest["objects"].is_array()) {
            manifest["objects"] = json::array();
        }

        bool found = false;
        for (const auto& k : manifest["objects"]) {
            if (k.is_string() && k.get<std::string>() == key) {
                found = true;
                break;
            }
        }
        if (!found) {
            manifest["objects"].push_back(key);
            jm.SaveJson(manifestPath, manifest);
        }

        if (onSaveNotification_) {
            onSaveNotification_("\"" + obj->GetName() + "\" を保存しました");
        }
    }
}
