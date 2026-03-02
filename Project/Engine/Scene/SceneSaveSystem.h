#pragma once

#include <string>
#include <functional>

namespace CoreEngine
{
    class GameObjectManager;
    class GameObject;

    /// @brief シーンのオブジェクトデータ JSON 保存/読み込みを担当するクラス
    class SceneSaveSystem {
    public:
        /// @brief シーン名を設定（JSON ファイルパスに使用）
        void SetSceneName(const std::string& name) { sceneName_ = name; }

        /// @brief シーン名を取得
        const std::string& GetSceneName() const { return sceneName_; }

        /// @brief シーンのオブジェクトデータを JSON から読み込んで登録済みオブジェクトに適用
        void Load(GameObjectManager* mgr);

        /// @brief シーンのオブジェクトデータを JSON に保存
        void Save(GameObjectManager* mgr);

        /// @brief 単一オブジェクトのデータだけを JSON に上書き保存
        void SaveSingle(GameObject* obj);

        /// @brief 保存完了時に呼ばれる通知コールバックを設定
        void SetSaveNotificationCallback(std::function<void(const std::string&)> cb) {
            onSaveNotification_ = std::move(cb);
        }

    private:
        std::string sceneName_;
        std::function<void(const std::string&)> onSaveNotification_;
    };
}
