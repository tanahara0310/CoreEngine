#pragma once

#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine::Reflection { struct TypeDescriptor; struct PropertyDescriptor; }

namespace CoreEngine
{
class GameObject;
class ComponentHost;

/// @brief GameObject にアタッチする機能単位の基底クラス。
/// @details 呼び出し順は Awake（Add直後）→ Start（初回更新）→ Update →
///          GameObject::Update → 全オブジェクトの後に LateUpdate → OnDestroy。
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

    /// @brief 毎フレームの更新（GameObject::Update() より前）
    virtual void Update() {}

    /// @brief 毎フレームの更新（GameObject::Update() より後）
    virtual void LateUpdate() {}

    /// @brief 取り外し時・オブジェクト破棄時に 1 回だけ呼ばれる
    /// @note 実体の解放はフレーム末まで遅延する（衝突コールバック中の着脱で
    ///       生ポインタが宙に浮かないようにするため）。
    virtual void OnDestroy() {}

    // ===== シリアライズ =====

    /// @brief このコンポーネントの状態を JSON へ書き出す
    /// @return 保存不要なら空の json を返す（呼び出し側が省略する）
    virtual json OnSerialize() const { return {}; }

    /// @brief JSON から状態を復元する
    virtual void OnDeserialize(const json& j) { (void)j; }

    /// @brief シリアライズ時の型キー
    /// @return `{"type": ここの文字列}` として保存される。プレハブ復元の型名にもなる。
    virtual const char* GetTypeName() const = 0;

    // ===== リフレクション =====

    /// @brief プロパティ一覧の記述子
    /// @return REFLECT_BEGIN を書いていない型は nullptr（呼び出し側は旧経路へ落ちる）
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

    // ===== インスペクター =====
#ifdef USE_IMGUI
    /// @brief インスペクタのセクション名
    /// @note 既定は GetTypeName() と同じ。日本語表示にしたい場合はオーバーライドする。
    virtual const char* GetInspectorName() const { return GetTypeName(); }

    /// @brief インスペクターの中身を描画する
    /// @return 値が変更されたら true
    virtual bool DrawInspector() { return false; }

    /// @brief プロパティの後に足す補足表示（派生値・単位のヒントなど）
    /// @note 記述子には書けない情報をここへ置く。記述子経由の描画でも
    ///       旧 DrawInspector でも、プロパティを描いた後に呼ばれる。
    virtual void DrawInspectorExtra() {}
#endif

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
