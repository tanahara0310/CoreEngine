#pragma once

#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine::Reflection { struct TypeDescriptor; struct PropertyDescriptor; }

namespace CoreEngine
{
class GameObject;
class ComponentHost;
struct CollisionInfo;

/// @brief GameObject にアタッチする機能単位の基底クラス。
/// @details 呼び出し順は Awake（Add直後）→ Start（初回更新）→ Update →
///          全オブジェクトの後に LateUpdate → 当たり判定の通知 → OnDestroy。
///          他コンポーネントの参照は Start 以降に `Sibling<T>()` で行う。
class IComponent {
public:
    virtual ~IComponent() = default;

    IComponent(const IComponent&) = delete;
    IComponent& operator=(const IComponent&) = delete;

    // ===== ライフサイクル =====

    /// @brief アタッチ直後に 1 回だけ呼ばれる
    /// @note この時点では**他のコンポーネントが揃っていない**（自分が最初の 1 個かもしれない）。
    ///       兄弟コンポーネントを参照する初期化は Start() で行うこと。
    virtual void Awake() {}

    /// @brief 最初の更新フレームで 1 回だけ呼ばれる
    /// @note 兄弟コンポーネントは揃っている。`Sibling<T>()` で取得できる。
    virtual void Start() {}

    /// @brief 毎フレームの更新
    virtual void Update() {}

    /// @brief 毎フレームの更新（全オブジェクトの Update の後）
    virtual void LateUpdate() {}

    /// @brief 取り外し時・オブジェクト破棄時に 1 回だけ呼ばれる
    /// @note 実体の解放はフレーム末まで遅延する（衝突コールバック中の着脱で
    ///       生ポインタが宙に浮かないようにするため）。
    virtual void OnDestroy() {}

    // ===== 当たり判定 =====
    // 持ち主のコライダーが他のコライダーと触れたときに呼ばれる（無効なコンポーネントには届かない）。
    // どちらかのコライダーがトリガーなら OnTrigger*、両方とも押し出す側なら OnCollision*。
    // 呼ばれるのは全オブジェクトの LateUpdate の後の判定の中。

    /// @brief 押し出す同士のコライダーが触れ始めた
    virtual void OnCollisionEnter(const CollisionInfo&) {}

    /// @brief 押し出す同士のコライダーが触れている（触れている間、毎フレーム）
    virtual void OnCollisionStay(const CollisionInfo&) {}

    /// @brief 押し出す同士のコライダーが離れた
    virtual void OnCollisionExit(const CollisionInfo&) {}

    /// @brief トリガーのコライダーと重なり始めた
    virtual void OnTriggerEnter(const CollisionInfo&) {}

    /// @brief トリガーのコライダーと重なっている（重なっている間、毎フレーム）
    virtual void OnTriggerStay(const CollisionInfo&) {}

    /// @brief トリガーのコライダーから離れた
    virtual void OnTriggerExit(const CollisionInfo&) {}

    // ===== シリアライズ =====

    /// @brief シリアライズ時の型キー
    /// @return `{"type": ここの文字列}` として保存される。プレハブ復元の型名にもなる。
    virtual const char* GetTypeName() const = 0;

    // ===== リフレクション =====

    /// @brief プロパティ一覧の記述子
    /// @return REFLECT_BEGIN を書いていない型は nullptr（保存は `IRawSavedParameters`、インスペクタはエディタの登録へ落ちる）
    virtual const Reflection::TypeDescriptor* GetTypeDescriptor() const { return nullptr; }

    /// @brief 記述子が想定する派生クラスの先頭アドレス
    /// @note 多重継承していると `IComponent*` と派生のアドレスがずれる。
    ///       記述子の resolve は派生を基準に組み立てるので、必ずこれを通す。
    virtual void* GetReflectionInstance() { return nullptr; }

    /// @brief 記述子経由でプロパティが書き換わった直後に呼ばれる
    /// @param property 書き換わったプロパティ
    /// @note 値から派生するもの（行列・キャッシュ）を作り直す場所。
    ///       インスペクタでの編集と Undo / Redo の適用の両方で呼ばれる。
    virtual void OnPropertyChanged(const Reflection::PropertyDescriptor& property)
    {
        (void)property;
    }

    // ===== 兄弟との依存 =====

    /// @brief 同じオブジェクトに付いた別のコンポーネントを使っているか
    /// @param other 同じオブジェクトに付いている別のコンポーネント
    /// @return true を返されたコンポーネントは、エディタから外せない
    /// @note 兄弟へのポインタを控えるコンポーネントは、控える型に対して true を返す。
    virtual bool RequiresComponent(const IComponent& other) const
    {
        (void)other;
        return false;
    }

    // ===== アクセサ =====

    /// @brief アタッチ先の GameObject
    GameObject* GetOwner() const { return owner_; }

    /// @brief 同じ GameObject にアタッチされた別のコンポーネントを取得する
    /// @tparam T 取得したいコンポーネント型（基底型でも引ける）
    /// @return 見つからなければ nullptr
    /// @note 定義は GameObject.h の末尾（GameObject が完全型になってから）。
    template <typename T>
    T* Sibling() const;

    /// @brief 有効か（false なら Update / LateUpdate をスキップする）
    bool IsEnabled() const { return isEnabled_; }
    void SetEnabled(bool enabled) { isEnabled_ = enabled; }

    /// @brief コードが付けたか
    /// @return シーン JSON・プレハブの復元とエディタの操作で付いたものは false
    bool IsAttachedByCode() const { return attachedByCode_; }

protected:
    IComponent() = default;

private:
    GameObject* owner_ = nullptr;
    bool        isEnabled_ = true;

    /// コードが付けたか（ComponentHost が付けるときに決める）
    bool attachedByCode_ = true;

    /// Start() を呼んだか（ComponentHost が管理）
    bool startCalled_ = false;

    friend class ComponentHost;
};
}
