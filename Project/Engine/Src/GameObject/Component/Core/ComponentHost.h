#pragma once

#include "IComponent.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace CoreEngine
{
class GameObject;

/// @brief コンポーネントを保持する器（`GameObject` が継承する）。
/// @details 実体は個別ヒープ確保なので追加しても既存の `T*` は無効化されず（参照安定）、
///          取り外しはスロットを nullptr 化してフレーム末に解放する（遅延解放）。
class ComponentHost {
public:
    ComponentHost() = default;
    /// @brief デストラクタ（保持しているコンポーネントを追加の逆順で破棄する）
    virtual ~ComponentHost();

    ComponentHost(const ComponentHost&) = delete;
    ComponentHost& operator=(const ComponentHost&) = delete;

    // ===== 追加 =====

    /// @brief コンポーネントを生成してアタッチする（以後の追加でも戻り値は無効化されない）
    /// @note `Awake()` はこの中で即座に呼ばれる。兄弟コンポーネントを見る初期化は `Start()` に書くこと。
    template <typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        static_assert(std::is_base_of_v<IComponent, T>,
            "T must derive from IComponent");

        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = owned.get();
        raw->owner_ = ownerObject_;
        components_.push_back(std::move(owned));
        raw->Awake();
        return raw;
    }

    // ===== 取得 =====

    /// @brief 指定型のコンポーネントを 1 個取得する（見つからなければ nullptr）
    /// @tparam T 具象型でも基底型でもよい。`IComponent` を継承していないミックスイン
    ///           （`ITransformSource` など）も `dynamic_cast` のクロスキャストで引ける。
    template <typename T>
    T* GetComponent() const {
        for (const auto& component : components_) {
            if (!component) { continue; }
            if (auto* typed = dynamic_cast<T*>(component.get())) {
                return typed;
            }
        }
        return nullptr;
    }

    /// @brief 指定型のコンポーネントをすべて取得する（同型を複数付けている場合）
    /// @tparam T `GetComponent` と同じく具象型・基底型・ミックスインのいずれでもよい
    template <typename T>
    std::vector<T*> GetComponents() const {
        std::vector<T*> result;
        for (const auto& component : components_) {
            if (!component) { continue; }
            if (auto* typed = dynamic_cast<T*>(component.get())) {
                result.push_back(typed);
            }
        }
        return result;
    }

    /// @brief 指定型のコンポーネントを持っているか
    template <typename T>
    bool HasComponent() const {
        return GetComponent<T>() != nullptr;
    }

    /// @brief 指定型のコンポーネントを取得し、無ければ生成する
    /// @note 移行期の互換 API（`GameObject::AddSphereCollider()` 等）が使う。
    template <typename T, typename... Args>
    T* GetOrAddComponent(Args&&... args) {
        if (auto* existing = GetComponent<T>()) {
            return existing;
        }
        return AddComponent<T>(std::forward<Args>(args)...);
    }

    /// @brief アタッチされているコンポーネント数（取り外し済みは含まない）
    size_t GetComponentCount() const;

    /// @brief 全コンポーネントを列挙する（**nullptr スロットが混ざりうる**ので要チェック）
    const std::vector<std::unique_ptr<IComponent>>& GetAllComponents() const {
        return components_;
    }

    // ===== 削除 =====

    /// @brief 指定したコンポーネントを取り外す（実体の解放はフレーム末）
    /// @return 見つかって取り外したら true
    bool RemoveComponent(IComponent* component);

    /// @brief 指定型のコンポーネントをすべて取り外す（実体の解放はフレーム末）
    /// @return 取り外した個数
    template <typename T>
    size_t RemoveComponents() {
        size_t removed = 0;
        for (auto& component : components_) {
            if (!component) { continue; }
            if (dynamic_cast<T*>(component.get())) {
                RetireSlot(component);
                ++removed;
            }
        }
        return removed;
    }

    /// @brief すべてのコンポーネントを取り外す（実体の解放はフレーム末）
    void RemoveAllComponents();

    /// @brief 取り外し済みコンポーネントの実体を解放し、配列を詰め直す
    /// @note `GameObjectManager` がフレーム末（衝突判定の後）に呼ぶ。
    void ReleaseRetiredComponents();

    // ===== ライフサイクル発行（GameObjectManager が呼ぶ） =====

    /// @brief まだ Start() を呼んでいないコンポーネントの Start() を呼ぶ
    void DispatchComponentStart();

    /// @brief 有効なコンポーネントの Update() を呼ぶ
    void DispatchComponentUpdate();

    /// @brief 有効なコンポーネントの LateUpdate() を呼ぶ
    void DispatchComponentLateUpdate();

    /// @brief 全コンポーネントの OnDestroy() を呼ぶ（オブジェクト破棄時）
    /// @note 実体はまだ解放しない。二重呼び出しはしない。
    void DispatchComponentDestroy();

protected:
    /// @brief オーナーの GameObject を登録する
    /// @note `GameObject` のコンストラクタが `this` を渡す。`ComponentHost` は
    ///       `GameObject` の定義を知らないので自分でキャストはできない。
    void SetOwnerObject(GameObject* owner) { ownerObject_ = owner; }

private:
    /// @brief スロットを retired_ へ移し、配列上は nullptr にする（インデックス不変）
    void RetireSlot(std::unique_ptr<IComponent>& slot);

    GameObject* ownerObject_ = nullptr;

    /// 生きているコンポーネント（実体は個別確保 = 参照が安定 / nullptr スロットがありうる）
    std::vector<std::unique_ptr<IComponent>> components_;

    /// 取り外し済みコンポーネントの墓場（フレーム末まで実体を保持する）
    std::vector<std::unique_ptr<IComponent>> retired_;

    /// OnDestroy() を発行済みか（二重発行の防止）
    bool destroyDispatched_ = false;
};
}
