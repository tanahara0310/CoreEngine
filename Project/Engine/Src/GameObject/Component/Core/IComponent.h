#pragma once

#include "Utility/JsonManager/JsonManager.h"

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

    // ===== インスペクター =====
#ifdef USE_IMGUI
    /// @brief インスペクターのタブ名
    /// @note 既定は GetTypeName() と同じ。日本語表示にしたい場合はオーバーライドする。
    virtual const char* GetInspectorName() const { return GetTypeName(); }

    /// @brief インスペクターの中身を描画する
    /// @return 値が変更されたら true
    virtual bool DrawInspector() { return false; }

    /// @brief インスペクタのタブアイコン（Engine/Assets/Textures/Icon 配下のファイル名）
    /// @note タブを持たないオブジェクトは「コンポーネント 1 個 = 1 タブ」として
    ///       インスペクタが組み立てられる。その左端に並ぶアイコン。
    virtual const char* GetInspectorIcon() const { return "obj.png"; }

    /// @brief タブアイコンの色（RGBA 0..1）
    /// @param outRgba 4 要素の配列
    virtual void GetInspectorIconColor(float* outRgba) const
    {
        outRgba[0] = 0.75f; outRgba[1] = 0.78f; outRgba[2] = 0.85f; outRgba[3] = 1.0f;
    }
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

protected:
    IComponent() = default;

private:
    GameObject* owner_ = nullptr;
    bool        isEnabled_ = true;

    /// Start() を呼んだか（ComponentHost が管理）
    bool startCalled_ = false;

    friend class ComponentHost;
};
}
