#include "pch.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "SceneSaveSystem.h"
#include "GameObject/Component/Core/ObjectRef.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Model/DynamicModelObject.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetRef.h"
#include "Reflection/PropertySerializer.h"
#include "Reflection/PropertyValue.h"
#include "Reflection/TypeDescriptor.h"
#include "Scene/PrefabSystem.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"

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

        std::string MakeSceneDir(const std::string& sceneName) {
            return "Application/Assets/Scenes/" + sceneName;
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
                            FindReferencedComponent(*target, p.acceptsComponent, ref->componentType)) {
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

            // 次回起動時にコード無しで復元できるよう型名を残す
            if (const char* typeName = object.GetSerializeTypeName()) {
                data["objectType"] = typeName;
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
    }


        /// @brief マニフェストに載っていないオブジェクト JSON を警告する
        /// @details SaveScene はマニフェストを毎回作り直すが、不要になった
        ///          オブジェクトの JSON ファイルは削除しない。読み込みは
        ///          マニフェスト経由だけなので、残ったファイルは黙って無視される。
        ///          「エディタで調整して保存したのに次回反映されない」という
        ///          原因の分かりにくい状態になるため、起動時に名前を挙げる。
        void WarnOrphanObjectFiles(const std::string& sceneName)
        {
            namespace fs = std::filesystem;
            auto& jm = JsonManager::GetInstance();

            const std::string manifestPath = MakeManifestPath(sceneName);
            if (!jm.FileExists(manifestPath)) {
                return;
            }

            json manifest = jm.LoadJson(manifestPath);
            if (!manifest.contains("objects") || !manifest["objects"].is_array()) {
                return;
            }

            std::unordered_set<std::string> known;
            for (const auto& entry : manifest["objects"]) {
                if (entry.is_string()) {
                    known.insert(entry.get<std::string>());
                }
            }

            std::error_code ec;
            const fs::path dir = Logger::GetInstance().Utf8ToPath(MakeSceneDir(sceneName));
            if (!fs::is_directory(dir, ec)) {
                return;
            }

            std::vector<std::string> orphans;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (ec) break;
                if (!entry.is_regular_file()) continue;

                const fs::path& file = entry.path();
                if (file.extension() != ".json") continue;

                // "_scene" / "_camera" などのメタファイルは対象外
                const std::string stem = Logger::GetInstance().PathToUtf8(file.stem());
                if (stem.empty() || stem.front() == '_') continue;

                if (known.find(stem) == known.end()) {
                    orphans.push_back(stem);
                }
            }

            if (orphans.empty()) {
                return;
            }

            std::string list;
            for (size_t i = 0; i < orphans.size(); ++i) {
                if (i) list += ", ";
                list += orphans[i];
            }
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                "SceneSaveSystem: \"{}\" にマニフェスト未登録の JSON が {} 件あります"
                "（読み込まれません）: {}",
                sceneName, orphans.size(), list);
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
            // コンポーネントの値が指すモデル（プレハブから作るものはプレハブの値を重ねてから探す）
            if (data.contains("prefab")) {
                const json expanded = PrefabSystem::ExpandInstanceJson(
                    data, PrefabSystem::LoadComponents(PrefabSystem::ReadPrefabRef(data)));
                if (expanded.contains("components")) {
                    CollectModelRefs(expanded.at("components"), modelPaths);
                }
            } else if (data.contains("components")) {
                CollectModelRefs(data.at("components"), modelPaths);
            }

            if (!data.contains("modelPath") || !data["modelPath"].is_string()) {
                return;
            }
            std::string modelPath = data["modelPath"].get<std::string>();
            if (modelPath.empty()) {
                return;
            }

            // 同じモデルを複数オブジェクトが共有するのが普通なので重複を潰す。
            // ここで潰さなくても ModelManager 側のロード権で 1 回に収束するが、
            // 無駄なタスクをスレッドプールへ積まない
            if (std::find(modelPaths.begin(), modelPaths.end(), modelPath) == modelPaths.end()) {
                modelPaths.push_back(std::move(modelPath));
            }
        });

        return modelPaths;
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
            json data = JsonManager::GetInstance().LoadJson(pending.path);
            if (!data.is_null() && pending.object) {
                if (loadManager_) {
                    RestoreObjectId(*loadManager_, *pending.object, data);
                }
                if (data.contains("prefab")) {
                    RestorePrefabInstance(sceneName_, *pending.object, data);
                } else {
                    pending.object->Deserialize(data);
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
        return true;
    }

    namespace
    {
        /// @brief 型名 → 生成関数。RegisterObjectType で埋まる
        /// @note 関数内 static にしてあるのは、他の翻訳単位の静的初期化から
        ///       登録されても順序問題が起きないようにするため
        std::unordered_map<std::string, SceneSaveSystem::ObjectFactory>& ObjectFactoryTable()
        {
            static std::unordered_map<std::string, SceneSaveSystem::ObjectFactory> table;
            return table;
        }
    }

    void SceneSaveSystem::RegisterObjectType(const std::string& typeName, ObjectFactory factory)
    {
        if (typeName.empty() || !factory) { return; }
        ObjectFactoryTable()[typeName] = std::move(factory);
    }

    void SceneSaveSystem::BeginLoad(GameObjectManager* mgr)
    {
        pendingObjects_.clear();
        loadIndex_ = 0;
        loadManager_ = mgr;

        if (sceneName_.empty() || !mgr) return;

        WarnOrphanObjectFiles(sceneName_);

        auto& jm = JsonManager::GetInstance();

        auto findObjectBySerializeKey = [mgr](const std::string& key) -> GameObject* {
            for (const auto& obj : mgr->GetAllObjects()) {
                if (obj && obj->GetSerializeKey() == key) {
                    return obj.get();
                }
            }
            return nullptr;
        };

        ForEachManifestObject(sceneName_,
            [mgr, &findObjectBySerializeKey](const std::string& key, const json& data) {
                // 既にシーン側が同じキーで生成済みならマニフェストからは作らない
                if (findObjectBySerializeKey(key)) {
                    return;
                }

                // 型名が入っていれば、登録済みファクトリから作る
                // （エディタ上で追加した UI を復活させる経路）
                if (data.contains("objectType") && data["objectType"].is_string()) {
                    const std::string typeName = data["objectType"].get<std::string>();
                    auto& table = ObjectFactoryTable();
                    if (auto it = table.find(typeName); it != table.end()) {
                        if (auto obj = it->second()) {
                            obj->SetName(key);
                            mgr->AddObject(std::move(obj));
                        }
                        return;
                    }
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                        "SceneSaveSystem: 未登録の型 \"{}\" は復元できません（key: {}）",
                        typeName, key);
                    return;
                }

                // プレハブから作るものは空のオブジェクトだけを置き、構成は復元時にプレハブから組む
                if (data.contains("prefab")) {
                    auto obj = std::make_unique<GameObject>();
                    obj->SetName(key);
                    mgr->AddObject(std::move(obj));
                    return;
                }

                if (data.contains("modelPath") && data["modelPath"].is_string()) {
                    auto obj = std::make_unique<DynamicModelObject>();
                    obj->SetModelPath(data["modelPath"].get<std::string>());
                    obj->SetName(key);
                    mgr->AddObject(std::move(obj));
                }
            });

        // 復元対象を確定させる（実際のデシリアライズは StepLoad が 1 体ずつ行う）
        for (const auto& obj : mgr->GetAllObjects()) {
            if (!obj || !obj->IsSerializeEnabled()) continue;
            const std::string& key = obj->GetSerializeKey();
            if (key.empty()) continue;

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

        // マニフェスト（オブジェクトキー一覧）
        json manifest;
        manifest["objects"] = json::array();

        // 各オブジェクトを個別ファイルに保存
        for (const auto& obj : mgr->GetAllObjects()) {
            if (!obj || !obj->IsSerializeEnabled()) continue;
            const std::string& key = obj->GetSerializeKey();
            if (key.empty()) continue;

            const json data = BuildObjectJson(*obj);
            if (!data.empty()) {
                jm.SaveJson(GetObjectPath(key), data);
                manifest["objects"].push_back(key);
            }
        }

        // マニフェストを保存
        jm.SaveJson(GetManifestPath(), manifest);

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
