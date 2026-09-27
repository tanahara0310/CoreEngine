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
    /// @details `Assets/Scenes/{sceneName}/` に、シーンの設定 `_scene.json` と
    ///          オブジェクト単位の `{serializeKey}.json`（並び順 `order` を持つ）を分けて置く。
    class SceneSaveSystem {
    public:
        /// @brief シーンの設定のファイル名
        static constexpr const char* kManifestFileName = "_scene.json";

        /// @brief 保存で 1 つのファイルをどうするか
        enum class FileWrite {
            Write,       ///< 書く（外で変わっていない・書く中身と同じ）
            KeepOutside, ///< 書かない（外で変わっていて、自分は変えていない）
            Conflict,    ///< 自分も外も変えている（書くと外の変更が消える）
        };

        /// @brief この実行で最後に読み書きした後に、外で変わったシーンのファイル
        struct ExternalChange {
            /// @brief 変わり方
            enum class Kind { Added, Changed, Removed };

            std::string fileName; ///< ファイル名（`Player.json` など）
            Kind kind = Kind::Changed;
        };

        /// @brief シーン名を設定（JSON ファイルパスに使用）
        void SetSceneName(const std::string& name) { sceneName_ = name; }

        /// @brief シーン名を取得
        const std::string& GetSceneName() const { return sceneName_; }

        /// @brief シーンのオブジェクトデータを JSON から読み込んで登録済みオブジェクトに適用
        /// @note 完了まで戻らない。フレームを回しながら読むなら BeginLoad / StepLoad を使う
        void Load(GameObjectManager* mgr);

        /// @brief 読み込みを開始する（保存データからの生成と復元対象の確定まで）
        /// @note フォルダにあるオブジェクトの JSON を並び順（order、同じなら保存キーの順）で読む。
        ///       読めない JSON は、エラーとして名前を挙げて飛ばす。
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

        /// @brief マニフェスト（`_scene.json`）に書くシーンの設定
        struct ManifestSettings {
            std::vector<std::string> features; ///< `features`：足す Feature の名前（並び順に足す）
            std::optional<bool> defaultGround; ///< `defaultGround`：既定の床を使うか（書かれていなければ空）

            /// `collision.pairs`：当たるレイヤーの組み合わせ（書かれていなければ空＝エンジンの既定とシーンのコードのまま）
            std::optional<std::vector<std::pair<std::string, std::string>>> collisionPairs;

            std::string cameraRig; ///< `cameraRig`：シーンの開始時に動かすカメラリグの名前（空なら動かさない）
        };

        /// @brief マニフェストの設定を読む（オブジェクトは生成しない）
        static ManifestSettings LoadManifestSettings(const std::string& sceneName);

        /// @brief マニフェストのシーンの設定を書き換える（ここで扱わない項目はそのまま）
        /// @param overwrite 外で変わっていても自分の値で書くか
        /// @return 書いたら true（外で変わったので書かなかったときは false）
        bool SaveManifestSettings(const ManifestSettings& settings, bool overwrite = false);

        /// @brief マニフェストのシーンの設定を書いたら、外の変更とぶつかるかを調べる（書かない）
        FileWrite CheckManifestSettings(const ManifestSettings& settings) const;

        /// @brief 保存データを持つシーンの名前（`Application/Assets/Scenes/<名前>/_scene.json` があるフォルダ）
        /// @return 名前順
        static std::vector<std::string> ListSavedScenes();

        /// @brief 新しいシーンのひな形
        enum class SceneTemplate {
            Empty,  ///< 空（オブジェクトなし。床と空はエンジンの既定が入る）
            Basic,  ///< 基本（太陽のオブジェクトと既定の床）
        };

        /// @brief シーン名として使えるか調べる
        /// @param name 調べる名前
        /// @param error 使えないときの理由（省略可）
        /// @return 使えるなら true
        /// @note 空・パスの区切りを含む・`_` で始まる・既に同じ名前があるものは使えない。
        static bool IsValidSceneName(const std::string& name, std::string* error = nullptr);

        /// @brief 新しいシーンのフォルダとマニフェストを作る
        /// @param sceneName シーン名（フォルダ名になる）
        /// @param templateKind ひな形
        /// @param error 作れなかったときの理由（省略可）
        /// @return 作れたら true
        static bool CreateScene(const std::string& sceneName, SceneTemplate templateKind,
                                std::string* error = nullptr);

        /// @brief シーン全体を保存（全オブジェクトの個別ファイル。古い形ならマニフェストの一覧も外す）
        /// @param overwrite 外で変わったファイルでも、自分も変えたものは自分の値で書く・消すか
        /// @note 並び順は、前に読み書きしたオブジェクトはその番号、新しいものは続きの番号にする。
        ///       この実行で読み書きしたのに今は無いオブジェクト（消したもの）の JSON は消す。
        ///       この実行で読み書きしていない JSON（ほかの人が足したものなど）は消さない。
        ///       外で変わったファイルは、自分が変えていなければ書かずに残す。
        ///       削除の印が付いたオブジェクトは保存しない。
        void SaveScene(GameObjectManager* mgr, bool overwrite = false);

        /// @brief 指定オブジェクト1体だけを個別ファイルに保存（外で変わっていたら書かない）
        void SaveObject(GameObject* obj);

        /// @brief 保存したら外の変更を消してしまうオブジェクトのファイル名（自分も外も変えた・自分が消したのに外で変わった）
        /// @note ファイルは書かない。
        std::vector<std::string> CheckSaveConflicts(const GameObjectManager& mgr) const;

        /// @brief シーンのフォルダの設定のファイル（`_scene.json` など）の今の中身を、この実行で最後に読み書きした中身として控える
        /// @note 読んだ直後と書いた直後に呼ぶ。ファイルが無ければ控えを消す。
        static void RememberSettingsFile(const std::string& sceneName, const std::string& fileName);

        /// @brief シーンのフォルダの設定のファイルを書く前に、外の変更とぶつかるかを決める
        /// @param text 書こうとしている中身（`JsonManager::ToFileText` の形）
        static FileWrite DecideSettingsFileWrite(const std::string& sceneName, const std::string& fileName,
                                                 const std::string& text);

        /// @brief シーンのフォルダを調べ、この実行で最後に読み書きした後に外で足された・変わった・消えたファイルを返す
        /// @note 対象はオブジェクトの JSON と、控えのある設定のファイル。ファイル名の順に並べる。
        static std::vector<ExternalChange> FindExternalChanges(const std::string& sceneName);

        /// @brief 保存完了時に呼ばれる通知コールバックを設定
        void SetSaveNotificationCallback(std::function<void(const std::string&)> cb) {
            onSaveNotification_ = std::move(cb);
        }

    private:
        // パス組み立てとオブジェクトのファイルの読み書きの実体は .cpp の無名名前空間にある

        /// @brief シーンフォルダのパスを返す  (例: "Application/Assets/Scenes/TestScene")
        std::string GetSceneDir() const;

        /// @brief マニフェストファイルのパスを返す  (例: ".../TestScene/_scene.json")
        std::string GetManifestPath() const;

        /// @brief 今のマニフェストにシーンの設定を当てた中身を作る（ここで扱わない項目はそのまま）
        json BuildManifest(const ManifestSettings& settings) const;

        /// @brief 復元待ちのオブジェクト 1 体分
        struct PendingObject {
            GameObject* object = nullptr;
            const json* data = nullptr; ///< 復元する値（restoreSnapshot_ の中を指す）
        };

        std::string sceneName_;
        std::function<void(const std::string&)> onSaveNotification_;
        std::vector<PendingObject> pendingObjects_;
        size_t loadIndex_ = 0;

        /// @brief 読み込み中のシーンのオブジェクト（復元し終えたら参照を確かめる）
        GameObjectManager* loadManager_ = nullptr;

        /// @brief 復元する値の元（保存ファイルから読むときは、読み始めにフォルダの中身から作る）
        std::shared_ptr<const SceneSnapshot> restoreSnapshot_;
    };
}
