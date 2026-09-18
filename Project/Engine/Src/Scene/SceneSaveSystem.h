#pragma once

#include "Utility/JsonManager/JsonManager.h"

#include <string>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace CoreEngine
{
    class GameObjectManager;
    class GameObject;

    /// @brief メモリに控えたシーンのオブジェクト（保存ファイルと同じ中身）
    struct SceneSnapshot
    {
        /// @brief オブジェクト 1 体分
        struct Object
        {
            std::string key; ///< 保存キー
            json data;       ///< `SaveScene` がファイルへ書くのと同じ JSON
        };

        std::vector<Object> objects; ///< 保存するときのマニフェストと同じ並び
    };

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
        /// @note 完了まで戻らない。フレームを回しながら読むなら BeginLoad / StepLoad を使う
        void Load(GameObjectManager* mgr);

        /// @brief 読み込みを開始する（マニフェストからの生成と復元対象の確定まで）
        /// @note マニフェストに無いオブジェクトの JSON があれば、エラーとして名前を挙げる。
        void BeginLoad(GameObjectManager* mgr);

        /// @brief 復元を 1 体分だけ進める
        /// @return true: 全ての復元が終わった
        bool StepLoad();

        /// @brief 復元の進捗（0.0〜1.0）
        float GetLoadProgress() const;

        /// @brief シーン JSON のコンポーネントが指すモデルのパスを列挙する（オブジェクトは一切生成しない）
        /// @return 重複を除いたモデルのパスのリスト
        /// @note GameObjectManager が要らないので、シーン構築より前（シェーダコンパイル中）に呼べる
        static std::vector<std::string> CollectModelPaths(const std::string& sceneName);

        /// @brief 控えのコンポーネントが指すモデルのパスを列挙する
        /// @return 重複を除いたモデルのパスのリスト
        static std::vector<std::string> CollectModelPaths(const SceneSnapshot& snapshot);

        /// @brief シーンのオブジェクトを、保存と同じ形でメモリに控える（ファイルは書かない）
        static std::shared_ptr<const SceneSnapshot> CaptureSnapshot(const GameObjectManager& mgr);

        /// @brief 読み込みの元を、保存ファイルではなくメモリの控えにする
        /// @param snapshot 空ならファイルから読む
        /// @note BeginLoad より前に呼ぶ。読み終えたら控えを手放す。
        void SetRestoreSnapshot(std::shared_ptr<const SceneSnapshot> snapshot) { restoreSnapshot_ = std::move(snapshot); }

        /// @brief マニフェスト（`_scene.json`）の、オブジェクトの一覧のほかに書ける設定
        struct ManifestSettings {
            std::vector<std::string> features; ///< `features`：足す Feature の名前（並び順に足す）
            std::optional<bool> defaultGround; ///< `defaultGround`：既定の床を使うか（書かれていなければ空）
        };

        /// @brief マニフェストの設定を読む（オブジェクトは生成しない）
        static ManifestSettings LoadManifestSettings(const std::string& sceneName);

        /// @brief 保存データを持つシーンの名前（`Application/Assets/Scenes/<名前>/_scene.json` があるフォルダ）
        /// @return 名前順
        static std::vector<std::string> ListSavedScenes();

        /// @brief シーン全体を保存（マニフェスト + 全オブジェクトの個別ファイル）
        /// @note マニフェストに載らなかったオブジェクトの JSON（名前が `_` で始まるものを除く）は消す。
        ///       削除の印が付いたオブジェクトは保存しない。
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

        /// @brief 復元待ちのオブジェクト 1 体分
        struct PendingObject {
            GameObject* object = nullptr;
            std::string path;          ///< 保存ファイルから読むときのパス
            const json* data = nullptr; ///< 控えから読むときの値（restoreSnapshot_ の中を指す）
        };

        std::string sceneName_;
        std::function<void(const std::string&)> onSaveNotification_;
        std::vector<PendingObject> pendingObjects_;
        size_t loadIndex_ = 0;

        /// @brief 読み込み中のシーンのオブジェクト（復元し終えたら参照を確かめる）
        GameObjectManager* loadManager_ = nullptr;

        /// @brief 保存ファイルの代わりに読む控え（空ならファイルから読む）
        std::shared_ptr<const SceneSnapshot> restoreSnapshot_;
    };
}
