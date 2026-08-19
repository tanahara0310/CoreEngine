#pragma once

#include <string>
#include <functional>
#include <vector>

namespace CoreEngine
{
    class GameObjectManager;
    class GameObject;

    /// @brief シーンのオブジェクトデータ JSON 保存 / 読み込みを担当するクラス
    /// @details `Assets/Scenes/{sceneName}/` にマニフェスト `_scene.json` と
    ///          オブジェクト単位の `{serializeKey}.json` を分けて置く。
    class SceneSaveSystem {
    public:
        /// @brief シーン名を設定（JSON ファイルパスに使用）
        void SetSceneName(const std::string& name) { sceneName_ = name; }

        /// @brief シーン名を取得
        const std::string& GetSceneName() const { return sceneName_; }

        /// @brief シーンのオブジェクトデータを JSON から読み込んで登録済みオブジェクトに適用
        void Load(GameObjectManager* mgr);

        /// @brief シーン JSON から modelPath だけを列挙する（オブジェクトは一切生成しない）
        /// @return 重複を除いた modelPath のリスト
        /// @note GameObjectManager が要らないので、シーン構築より前（シェーダコンパイル中）に呼べる
        static std::vector<std::string> CollectModelPaths(const std::string& sceneName);

        /// @brief シーン全体を保存（マニフェスト + 全オブジェクトの個別ファイル）
        void SaveScene(GameObjectManager* mgr);

        /// @brief 指定オブジェクト1体だけを個別ファイルに保存
        void SaveObject(GameObject* obj);

        /// @brief 保存完了時に呼ばれる通知コールバックを設定
        void SetSaveNotificationCallback(std::function<void(const std::string&)> cb) {
            onSaveNotification_ = std::move(cb);
        }

    private:
        // パス組み立てとマニフェスト走査の実体は .cpp の無名名前空間にある
        //（Load と CollectModelPaths が同じスキーマ解析を共有するため）

        /// @brief シーンフォルダのパスを返す  (例: "Application/Assets/Scenes/TestScene")
        std::string GetSceneDir() const;

        /// @brief マニフェストファイルのパスを返す  (例: ".../TestScene/_scene.json")
        std::string GetManifestPath() const;

        /// @brief 個別オブジェクトファイルのパスを返す  (例: ".../TestScene/Model_0.json")
        std::string GetObjectPath(const std::string& key) const;

        std::string sceneName_;
        std::function<void(const std::string&)> onSaveNotification_;
    };
}
