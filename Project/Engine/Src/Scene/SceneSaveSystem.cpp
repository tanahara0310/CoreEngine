#include "pch.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "SceneSaveSystem.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Model/DynamicModelObject.h"
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
        if (loadIndex_ >= pendingObjects_.size()) {
            pendingObjects_.clear();
            loadIndex_ = 0;
            return true;
        }

        const PendingObject& pending = pendingObjects_[loadIndex_++];
        json data = JsonManager::GetInstance().LoadJson(pending.path);
        if (!data.is_null() && pending.object) {
            pending.object->OnDeserialize(data);
        }

        return loadIndex_ >= pendingObjects_.size();
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

            json data = obj->OnSerialize();
            if (!data.empty()) {
                // 次回起動時にコード無しで復元できるよう型名を残す
                if (const char* typeName = obj->GetSerializeTypeName()) {
                    data["objectType"] = typeName;
                }
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
        json data = obj->OnSerialize();
        if (!data.empty()) {
            // 次回起動時にコード無しで復元できるよう型名を残す
            if (const char* typeName = obj->GetSerializeTypeName()) {
                data["objectType"] = typeName;
            }
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
