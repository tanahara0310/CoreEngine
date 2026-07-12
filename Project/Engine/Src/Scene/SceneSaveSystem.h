#pragma once

#include <string>
#include <functional>

namespace CoreEngine
{
    class GameObjectManager;
    class GameObject;

    /// @brief シーンのオブジェクトデータ JSON 保存/読み込みを担当するクラス
    ///
    /// データはオブジェクト単位でファイル分割し、1つのシーンフォルダにまとめて管理する。
    /// @code
    /// Application/Assets/Scenes/
    ///   {sceneName}/                  ← シーンフォルダ
    ///     _scene.json                  ← マニフェスト（version + オブジェクトキー一覧）
    ///     {serializeKey}.json          ← 個別オブジェクトデータ
    /// @endcode
    class SceneSaveSystem {
    public:
        /// @brief シーン名を設定（JSON ファイルパスに使用）
        void SetSceneName(const std::string& name) { sceneName_ = name; }

        /// @brief シーン名を取得
        const std::string& GetSceneName() const { return sceneName_; }

        /// @brief シーンのオブジェクトデータを JSON から読み込んで登録済みオブジェクトに適用
        void Load(GameObjectManager* mgr);

        /// @brief シーン全体を保存（マニフェスト + 全オブジェクトの個別ファイル）
        void SaveScene(GameObjectManager* mgr);

        /// @brief 指定オブジェクト1体だけを個別ファイルに保存
        void SaveObject(GameObject* obj);

        /// @brief 保存完了時に呼ばれる通知コールバックを設定
        void SetSaveNotificationCallback(std::function<void(const std::string&)> cb) {
            onSaveNotification_ = std::move(cb);
        }

    private:
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
