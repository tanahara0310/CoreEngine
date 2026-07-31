#pragma once

#include "Graphics/Render/RenderPassType.h"
#include "Graphics/Render/RenderItem.h"
#include "Graphics/Render/DrawViewInfo.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Math/Vector/Vector3.h"
#include "Collider/Collider.h"
#include "Utility/JsonManager/JsonManager.h"
#include <functional>
#include <d3d12.h>
#include <memory>
#include <optional>
#include <string>

#include "GameObject/IObjectSpawner.h"

// Forward declaration
namespace CoreEngine {
    class ICamera;
    class EngineSystem;
}

/// @brief ゲームワールドに存在するすべてのオブジェクトの共通基底クラス
/// @note モデルを持つオブジェクトは ModelGameObject を経由して継承する。
///       スプライトは SpriteObject を経由して継承する。

namespace CoreEngine
{
    class GameObject {
    public:
        virtual ~GameObject() = default;

        // ===== ライフサイクル =====

        /// @brief エンジンシステムへの静的参照を設定する
        /// @param engine アプリケーション全体で共有するエンジンシステムへのポインタ
        /// @note アプリケーション起動時に一度だけ呼び出す。
        ///       以降は GetEngineSystem() で取得できる。
        static void SetEngine(EngineSystem* engine);

        /// @brief オブジェクト固有の初期化処理
        /// @note GameObjectManager::AddObject() の内部で自動的に呼び出される。
        ///       派生クラスでオーバーライドして固有の初期化処理を実装する。
        virtual void Initialize() {}

        /// @brief 毎フレームの更新処理
        /// @note GameObjectManager::UpdateAll() から autoUpdate_ が true のとき自動的に呼ばれる。
        ///       派生クラスでオーバーライドして固有のロジックを実装する。
        virtual void Update();

        /// @brief 毎フレームの描画処理
        /// @param camera 描画に使用するカメラ（2Dオブジェクトは nullptr 可）
        /// @note 派生クラスでオーバーライドして描画コマンドを発行する。
        virtual void Draw(const ICamera* camera);

        /// @brief ビュー/パス情報つきの描画処理（RenderManager からの本経路）
        /// @param view カメラ・ビュー種別・パス種別をまとめた描画コンテキスト
        /// @note 既定実装は Draw(カメラ) へ委譲する。パス/ビュー種別で挙動を
        ///       変えるオブジェクト（ModelGameObject 等）のみオーバーライドする。
        virtual void Draw(const DrawViewInfo& view) { Draw(view.GetCamera()); }

        // ===== アクティブ =====

        /// @brief アクティブ状態を設定する
        /// @param active true にするとオブジェクトの更新・描画が有効になる
        void SetActive(bool active);

        /// @brief アクティブ状態を取得する
        /// @return true: アクティブ / false: 非アクティブ（更新・描画ともにスキップ）
        bool IsActive() const;

        // ===== 破棄 =====

        /// @brief オブジェクトに削除マークをつける
        /// @note フレーム末の CleanupDestroyed() で実際にメモリから削除される。
        ///       Update() 内部から安全に呼び出せる。
        void Destroy();

        /// @brief 削除マークが付いているか確認する
        /// @return true: このフレーム末に削除される
        bool IsMarkedForDestroy() const;

        // ===== 描画制御 =====

        /// @brief 描画順序オーバーライドを設定する
        /// @param order 小さいほど先に描画される。RenderPassType の優先度より優先される。
        void SetRenderOrder(int order);

        /// @brief 描画順序オーバーライドを取得する
        /// @return オーバーライド値。未設定の場合は std::nullopt を返す。
        std::optional<int> GetRenderOrder() const;

        /// @brief 描画順序オーバーライドをリセットする
        /// @note リセット後は RenderPassType の優先度で描画順が決まる。
        void ResetRenderOrder();

        /// @brief 所属する描画パスを返す
        /// @return デフォルトは RenderPassType::Model。派生クラスでオーバーライドして変更する。
        virtual RenderPassType GetRenderPassType() const;

        /// @brief 使用するブレンドモードを返す
        /// @return デフォルトは kBlendModeNone。α合成が必要な派生クラスでオーバーライドする。
        virtual BlendMode GetBlendMode() const;

        /// @brief ブレンドモードを設定する
        /// @param blendMode 設定するブレンドモード
        /// @note 派生クラスで保持・適用する。基底クラスの実装は何もしない。
        virtual void SetBlendMode(BlendMode blendMode);

        /// @brief 現在の描画状態から RenderItem を構築する
        /// @return RenderManager に登録する描画項目
        virtual RenderItem BuildRenderItem() const;

        // ===== エンジン参照 =====

        /// @brief エンジンシステムへのポインタを返す
        /// @return Initialize() で設定されたエンジンシステム。未初期化なら nullptr。
        EngineSystem* GetEngineSystem() const;

        // ===== 衝突イベント =====

        /// @brief 衝突開始時に一度だけ呼ばれるイベント
        /// @param other 衝突相手のゲームオブジェクト
        virtual void OnCollisionEnter(GameObject* other);

        /// @brief 衝突が継続している間、毎フレーム呼ばれるイベント
        /// @param other 衝突相手のゲームオブジェクト
        virtual void OnCollisionStay(GameObject* other);

        /// @brief 衝突が終了したときに一度だけ呼ばれるイベント
        /// @param other 衝突相手のゲームオブジェクト
        virtual void OnCollisionExit(GameObject* other);

        /// @brief ワールド空間での位置を返す
        /// @return コライダーシステムが参照する位置。
        ///         ModelGameObject は transform_ の位置を返す。基底クラスは {} を返す。
        virtual Vector3 GetWorldPosition() const;

        // ===== コライダー簡易設定 API =====

        /// @brief 球体コライダーを生成してアタッチする
        /// @param radius 球の半径（ワールド空間単位）
        /// @param layer 衝突判定に使うレイヤー。デフォルトは CollisionLayer::Default。
        /// @return アタッチされたコライダーへの参照（メソッドチェーン用）
        Collider& AddSphereCollider(float radius, CollisionLayer layer = CollisionLayer::Default);

        /// @brief AABB コライダーを生成してアタッチする
        /// @param size 各軸のサイズ（幅・高さ・奥行き）
        /// @param layer 衝突判定に使うレイヤー。デフォルトは CollisionLayer::Default。
        /// @return アタッチされたコライダーへの参照（メソッドチェーン用）
        Collider& AddAABBCollider(const Vector3& size, CollisionLayer layer = CollisionLayer::Default);

        /// @brief コライダーが設定されているか確認する
        /// @return true: コライダーがアタッチ済み
        bool HasCollider() const;

        /// @brief アタッチされたコライダーへのポインタを返す
        /// @return コライダーへのポインタ。未設定の場合は nullptr。
        Collider* GetCollider();

        /// @brief アタッチされたコライダーへの const ポインタを返す
        /// @return コライダーへの const ポインタ。未設定の場合は nullptr。
        const Collider* GetCollider() const;

        /// @brief コライダーを取り外して破棄する
        void RemoveCollider();

        // ===== 名前 / シリアライズ =====

        /// @brief オブジェクト識別名を設定する
        /// @param name ImGui 表示やシーン保存のキーとして使われる名前
        /// @note 初回呼び出し時にシリアライズキーも同時に設定される。
        ///       以降の呼び出しでは表示名のみ変更される。
        void SetName(const std::string& name);

        /// @brief オブジェクト識別名を返す
        /// @return SetName() で設定した名前。未設定なら空文字列。
        const std::string& GetName() const;

        /// @brief シリアライズ用の安定キーを返す
        /// @return 初回 SetName() で設定された名前。ユーザーがリネームしても変わらない。
        const std::string& GetSerializeKey() const;

        /// @brief 表示用の名前を返す（フォールバック付き）
        /// @return name_ → serializeKey_ → GetObjectName() の優先度で返す。
        const char* GetDisplayName() const;

        /// @brief オブジェクト種別名を返す
        /// @return クラスを表す文字列リテラル。シリアライズキーや ImGui 表示に使う。
        ///         派生クラスでオーバーライドして固有の名前を返すことを推奨する。
        virtual const char* GetObjectName() const;

        /// @brief JSON シリアライズ対象かどうかを返す
        /// @return true: シーン保存時にこのオブジェクトのデータが書き出される
        bool IsSerializeEnabled() const;

        /// @brief JSON シリアライズ対象かどうかを設定する
        /// @param enable false にするとシーンデータへの保存・復元がスキップされる
        void SetSerializeEnabled(bool enable);

        /// @brief オブジェクトデータを JSON に書き出す
        /// @return シリアライズ結果。保存不要な場合は空の json を返す。
        /// @note SceneSaveSystem から自動的に呼び出される。
        ///       派生クラスでオーバーライドして自分の保存処理を実装する。
        virtual json OnSerialize() const { return {}; }

        /// @brief JSON からオブジェクトデータを復元する
        /// @param j 読み込み元の JSON オブジェクト
        /// @note SceneSaveSystem から自動的に呼び出される。
        ///       派生クラスでオーバーライドして自分の復元処理を実装する。
        virtual void OnDeserialize(const json& j) { (void)j; }

        // ===== スポーン =====

        /// @brief 同じシーンに新しいオブジェクトをスポーンする
        /// @tparam T GameObject 派生クラス
        /// @tparam Args T のコンストラクタ引数の型パック
        /// @param args T のコンストラクタに転送する引数
        /// @return 生成・登録されたオブジェクトへのポインタ（所有権は GameObjectManager が持つ）
        /// @note GameObjectManager::AddObject() で登録済みのオブジェクトからのみ呼び出せる。
        ///       Update() 中に呼んでも安全（pending キューに積まれ次フレームから有効になる）。
        template<typename T, typename... Args>
        T* Spawn(Args&&... args) {
            static_assert(std::is_base_of_v<GameObject, T>,
                "T must derive from GameObject");
            return static_cast<T*>(
                spawner_->SpawnRaw(std::make_unique<T>(std::forward<Args>(args)...))
                );
        }

#ifdef USE_IMGUI
        // ===== プロパティインスペクター =====

        /// @brief インスペクタータブの定義情報
        struct InspectorTabDef {
            const char* iconPath;    ///< アイコンテクスチャのファイル名
            const char* tooltip;     ///< ツールチップテキスト
            float tint[4];           ///< アイコン色 {R, G, B, A}
            float selectedBg[4];     ///< 選択時背景色 {R, G, B, A}
        };

        /// @brief ImGui インスペクター UI を描画する（共通フレームワーク）
        /// @return 値の変更があった場合 true を返す
        /// @note 名前フィールド、Active トグル、タブストリップ、保存ボタンを自動描画する。
        ///       タブの内容は GetInspectorTabs / DrawInspectorTabContent で制御する。
        virtual bool DrawImGui();

        /// @brief タブ未対応オブジェクト用のフォールバック描画
        /// @return 値の変更があった場合 true を返す
        /// @note GetInspectorTabs が 0 を返す場合にのみ呼び出される。
        virtual bool DrawImGuiExtended();

        /// @brief インスペクタータブの定義を取得する
        /// @param outTabs 出力先配列
        /// @param maxTabs 配列の最大要素数
        /// @return タブ数（0 の場合はタブなしで DrawImGuiExtended にフォールバック）
        virtual int GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const {
            (void)outTabs; (void)maxTabs; return 0;
        }

        /// @brief 指定タブのコンテンツを描画する
        /// @param tabIndex タブインデックス
        /// @return 値が変更された場合 true
        virtual bool DrawInspectorTabContent(int tabIndex) {
            (void)tabIndex; return false;
        }

        /// @brief ImGui 編集コミット時コールバックの型
        /// @note 編集前のトランスフォームを引数として受け取り、Undo/Redo システムへ渡す。
        using EditCommitCallback = std::function<void(
            GameObject*            /* 対象オブジェクト */,
            const Vector3&         /* 編集前の位置 */,
            const Vector3&         /* 編集前の回転 */,
            const Vector3&         /* 編集前のスケール */,
            bool                   /* 編集前のアクティブ状態 */)>;

        /// @brief 編集コミット時コールバックを設定する
        /// @param cb SceneDebugEditor が設定する Undo/Redo 記録用コールバック
        void SetEditCommitCallback(EditCommitCallback cb);

        /// @brief 個別保存リクエストコールバックの型
        using SaveRequestCallback = std::function<void(GameObject*)>;

        /// @brief 個別保存リクエストコールバックを設定する
        /// @param cb 「このオブジェクトのみ保存」ボタン押下時に呼ばれるコールバック
        void SetSaveRequestCallback(SaveRequestCallback cb);
#endif

    protected:
        std::unique_ptr<Collider> collider_;     ///< アタッチされたコライダー
        std::string               name_;          ///< オブジェクト表示名（ユーザー編集可能）
        std::string               serializeKey_;  ///< シリアライズ用安定キー（初回 SetName で固定）

        bool isActive_ = true;   ///< アクティブ状態（false: 更新・描画ともにスキップ）
        bool markedForDestroy_ = false;  ///< 削除マーク（true: フレーム末に破棄）
        bool shouldSerialize_ = true;   ///< JSON シリアライズ対象フラグ

        std::optional<int> renderOrder_;  ///< 描画順序オーバーライド（nullopt: パス優先度に従う）

#ifdef USE_IMGUI
        EditCommitCallback  onEditCommitted_;   ///< 編集確定時コールバック
        SaveRequestCallback onSaveRequested_;   ///< 個別保存ボタン用コールバック

        int inspectorTab_ = 0;  ///< 現在選択中のインスペクタータブインデックス

        /// @brief Active チェックボックス変更時に呼び出されるフック
        /// @param prevActive 変更前のアクティブ状態
        /// @note Undo/Redo を記録したい派生クラス（ModelGameObject / SpriteObject）でオーバーライドする。
        virtual void OnImGuiActiveChanged(bool prevActive) { (void)prevActive; }

        /// @brief 「このオブジェクトのみ保存」ボタンを ImGui に描画する
        /// @note shouldSerialize_ が false または name_ が空の場合は何も描画しない。
        ///       DrawImGui() の末尾から呼ばれる。
        void DrawSaveButton();
#endif

    private:
        IObjectSpawner* spawner_ = nullptr;  ///< AddObject 時に GameObjectManager が注入するスポーナー

        friend class GameObjectManager;  ///< spawner_ への書き込みを許可
    };
}
