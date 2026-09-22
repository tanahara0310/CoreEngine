#pragma once

#include "GameObject/Component/Core/ComponentHost.h"
#include "GameObject/IObjectSpawner.h"
#include "GameObject/ObjectId.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Render/RenderItem.h"
#include "Graphics/Render/RenderPassType.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Math/Vector/Vector3.h"
#include "Utility/JsonManager/JsonManager.h"

#include <memory>
#include <optional>
#include <string>

namespace CoreEngine {
    class EngineSystem;
    class GameObjectManager;
    struct CollisionInfo;
    struct DrawViewInfo;
}

namespace CoreEngine
{
    /// @brief ID と名前を持ったコンポーネントの器
    /// @details 機能は継承ではなく `AddComponent<T>()` で載せる。描画・当たり判定・トランスフォームは
    ///          コンポーネントが受け持ち、この型は「どのコンポーネントを持つか」と
    ///          「シーンの中でどれか（ID・名前）」だけを持つ。
    class GameObject : public ComponentHost {
    public:
        /// @brief コンストラクタ
        /// @note コンポーネント基盤へ自分を所有者として登録する。以降 `AddComponent<T>()`
        ///       で載せたコンポーネントには `GetOwner()` からここへ辿れる。
        GameObject() {
            SetOwnerObject(this);
        }

        ~GameObject() = default;

        // ===== エンジン参照 =====

        /// @brief エンジンシステムへの静的参照を設定する
        /// @param engine アプリケーション全体で共有するエンジンシステムへのポインタ
        /// @note アプリケーション起動時に一度だけ呼び出す。
        static void SetEngine(EngineSystem* engine);

        /// @brief エンジンシステムへのポインタを返す（未設定なら nullptr）
        EngineSystem* GetEngineSystem() const;

        // ===== アクティブ・破棄 =====

        /// @brief アクティブ状態を設定する（false で更新・描画ともにスキップ）
        void SetActive(bool active);

        /// @brief アクティブ状態を取得する
        bool IsActive() const;

        /// @brief 削除マークをつける（実際の削除はフレーム末）
        /// @note Update() 内部から安全に呼び出せる。
        void Destroy();

        /// @brief 削除マークが付いているか
        bool IsMarkedForDestroy() const;

        // ===== 描画 =====

        /// @brief 描画コンポーネントへ描画を任せる（RenderManager からの入口）
        void Draw(const DrawViewInfo& view);

        /// @brief 現在の状態から RenderItem を構築する
        RenderItem BuildRenderItem() const;

        /// @brief 所属する描画パス（描画コンポーネントが答える。無ければ Model）
        RenderPassType GetRenderPassType() const;

        /// @brief ブレンドモード（描画コンポーネントが答える。無ければ なし）
        BlendMode GetBlendMode() const;

        /// @brief ブレンドモードを設定する（描画コンポーネントへ渡す）
        void SetBlendMode(BlendMode blendMode);

        /// @brief 描画順序オーバーライドを設定する（小さいほど先に描かれる）
        void SetRenderOrder(int order);

        /// @brief 描画順序オーバーライド（未設定なら std::nullopt）
        std::optional<int> GetRenderOrder() const;

        /// @brief 描画順序オーバーライドをリセットする
        void ResetRenderOrder();

        // ===== 当たり判定 =====

        /// @brief 接触の開始を伝える（`Collider` が呼び、コライダーの購読者と有効なコンポーネントへ配る）
        void NotifyCollisionEnter(const CollisionInfo& info);

        /// @brief 接触が続いていることを伝える
        void NotifyCollisionStay(const CollisionInfo& info);

        /// @brief 接触の終わりを伝える
        void NotifyCollisionExit(const CollisionInfo& info);

        /// @brief 衝突解決による移動を受け入れる
        /// @return 実際に動かせたら true。false なら解決側はもう一方を全量押し出す
        /// @note `TransformComponent` があればそこへ委譲し、無ければ false
        bool TryApplyCollisionPush(const Vector3& delta);

        /// @brief ワールド空間での位置（コライダー・ピッキングが参照する位置）
        /// @note `TransformComponent` → `ITransformSource` の順に読み、どちらも無ければ原点
        Vector3 GetWorldPosition() const;

        /// @brief ワールド空間でのスケール（コライダーのサイズ／半径に乗る）
        Vector3 GetWorldScale() const;

        /// @brief ワールド空間での向き（正規化した 3 本の軸）
        /// @param axisX X 軸の向きの書き出し先
        /// @param axisY Y 軸の向きの書き出し先
        /// @param axisZ Z 軸の向きの書き出し先
        /// @note `TransformComponent` が無ければワールド軸をそのまま返す。
        void GetWorldAxes(Vector3& axisX, Vector3& axisY, Vector3& axisZ) const;

        // ===== 識別子 =====

        /// @brief シーン内で重複しない ID
        /// @note `GameObjectManager` への登録時に保存キーから決まり、保存したシーンを読むと保存時の値へ戻る。
        ObjectId GetObjectId() const { return objectId_; }

        /// @brief 登録先の `GameObjectManager`（未登録なら nullptr）
        GameObjectManager* GetObjectManager() const { return objectManager_; }

        // ===== プレハブ =====

        /// @brief 元になったプレハブ（プレハブから作っていなければ何も指さない）
        const AssetRef<PrefabAsset>& GetPrefab() const { return prefab_; }

        /// @brief 元になったプレハブを設定する
        /// @note 設定したオブジェクトは、シーンへプレハブとの差分だけが保存される。
        void SetPrefab(const Reflection::AssetRefValue& prefab) { prefab_.SetValue(prefab); }

        /// @brief プレハブから作ったオブジェクトか
        bool IsPrefabInstance() const { return prefab_.IsSet(); }

        // ===== 名前 =====

        /// @brief オブジェクト識別名を設定する
        /// @note 初回呼び出し時に保存キーも同時に設定される。以降は表示名のみ変わる。
        void SetName(const std::string& name);

        /// @brief オブジェクト識別名（未設定なら空文字列）
        const std::string& GetName() const;

        /// @brief 保存用の安定キー（初回 `SetName()` の名前。表示名を変えても変わらない）
        const std::string& GetSerializeKey() const;

        /// @brief 表示用の名前（名前 → 保存キー → "GameObject" の順）
        const char* GetDisplayName() const;

        /// @brief 保存キーを差し替える
        /// @note 1 シーンで重複しないよう `GameObjectManager` が登録時に調整する。
        ///       消したオブジェクトを作り直すときは、元のキーへ戻すために呼ぶ。
        void SetSerializeKey(const std::string& key) { serializeKey_ = key; }

        // ===== シリアライズ =====

        /// @brief シーン保存の対象か
        bool IsSerializeEnabled() const;

        /// @brief シーン保存の対象かを設定する
        void SetSerializeEnabled(bool enable);

        /// @brief オブジェクトを JSON へ書き出す（SceneSaveSystem が呼ぶ唯一の入口）
        /// @return 有効・名前・コンポーネント一覧
        json Serialize() const;

        /// @brief JSON からオブジェクトを復元する（SceneSaveSystem が呼ぶ唯一の入口）
        void Deserialize(const json& j);

        // ===== スポーン =====

        /// @brief 同じシーンに新しいオブジェクトを作る（所有権は GameObjectManager）
        /// @note `GameObjectManager` へ登録済みのオブジェクトからのみ呼べる。
        ///       Update() 中に呼んでも安全（次フレームから有効になる）。
        GameObject* Spawn() {
            return spawner_ ? spawner_->SpawnRaw(std::make_unique<GameObject>()) : nullptr;
        }

    private:
        std::string               name_;          ///< オブジェクト表示名（ユーザー編集可能）
        std::string               serializeKey_;  ///< 保存用の安定キー（初回 SetName で固定）

        bool isActive_ = true;           ///< アクティブ状態（false: 更新・描画ともにスキップ）
        bool markedForDestroy_ = false;  ///< 削除マーク（true: フレーム末に破棄）
        bool shouldSerialize_ = true;    ///< シーン保存の対象か

        std::optional<int> renderOrder_;  ///< 描画順序オーバーライド（nullopt: パス優先度に従う）

        IObjectSpawner* spawner_ = nullptr;  ///< AddObject 時に GameObjectManager が注入するスポーナー
        GameObjectManager* objectManager_ = nullptr;  ///< 登録先（AddObject 時に注入される）
        ObjectId objectId_{};  ///< シーン内の ID（登録時に GameObjectManager が決める）
        AssetRef<PrefabAsset> prefab_;  ///< 元になったプレハブ

        friend class GameObjectManager;  ///< spawner_ / objectManager_ / objectId_ への書き込みを許可
    };

    // ===== IComponent のインライン定義 =====
    // GameObject が完全型になってから定義する必要があるものをここに置く。

    template <typename T>
    T* IComponent::Sibling() const {
        return owner_ ? owner_->GetComponent<T>() : nullptr;
    }
}
