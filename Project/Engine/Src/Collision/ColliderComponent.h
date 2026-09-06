#pragma once

#include "Collider.h"
#include "CollisionInfo.h"
#include "GameObject/Component/Core/IComponent.h"

#include <functional>
#include <memory>
#include <vector>

namespace CoreEngine
{
class GameObject;

/// @brief 1 つの GameObject が持つコライダーの集合（0 本以上・レイヤー別に複数可）。
/// @details Collider は個別ヒープ確保なので Add() が返す参照は以後も無効化されない。
///          Remove 系は実体を即 delete せず retired_ へ退避し、フレーム末に解放する
///          （衝突コールバック中の着脱で判定ループの生ポインタが宙に浮かないため）。
class ColliderComponent : public IComponent {
public:
    const char* GetTypeName() const override { return "Collider"; }

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "コライダー"; }

    const char* GetInspectorIcon() const override { return "obj.png"; }

    void GetInspectorIconColor(float* outRgba) const override
    {
        outRgba[0] = 0.35f; outRgba[1] = 0.85f; outRgba[2] = 0.45f; outRgba[3] = 1.0f;
    }

    /// @brief コライダーの形状・レイヤー編集 UI
    /// @return 値が変更されたら true
    /// @note 実体は ColliderInspector（ModelGameObject のタブと同じものを共有する）。
    bool DrawInspector() override;
#endif

    // ===== 追加 =====

    /// @brief 形状を指定してコライダーを追加する
    /// @return 追加されたコライダーへの参照（以後の追加でも無効化されない）
    Collider& Add(const CollisionShape& shape, CollisionLayer layer = CollisionLayer::Default);

    /// @brief 球コライダーを追加する
    Collider& AddSphere(float radius, CollisionLayer layer = CollisionLayer::Default,
                        const Vector3& offset = {});

    /// @brief ボックスコライダーを追加する
    Collider& AddBox(const Vector3& size, CollisionLayer layer = CollisionLayer::Default,
                     const Vector3& offset = {});

    // ===== 衝突イベントの購読 =====
    // 継承（GameObject::OnCollisionEnter の override）なしで衝突に反応するための入口。
    // `CollisionWorld` はオーナーの仮想関数を呼び、その既定実装がここへ配る。

    using CollisionCallback = std::function<void(const CollisionInfo&)>;

    void SetOnEnter(CollisionCallback callback) { onEnter_ = std::move(callback); }
    void SetOnStay(CollisionCallback callback) { onStay_ = std::move(callback); }
    void SetOnExit(CollisionCallback callback) { onExit_ = std::move(callback); }

    /// @brief 購読者へイベントを配る（`GameObject` の既定実装から呼ばれる）
    void DispatchEnter(const CollisionInfo& info) const { if (onEnter_) onEnter_(info); }
    void DispatchStay(const CollisionInfo& info) const { if (onStay_) onStay_(info); }
    void DispatchExit(const CollisionInfo& info) const { if (onExit_) onExit_(info); }

    // ===== 削除 =====

    /// @brief すべてのコライダーを取り外す（実体の解放はフレーム末）
    void RemoveAll();

    /// @brief 指定したコライダーを取り外す（実体の解放はフレーム末）
    /// @return 見つかって取り外したら true
    bool Remove(Collider* collider);

    /// @brief 取り外し済みコライダーの実体を解放する
    /// @note 衝突判定より後（GameObjectManager::CleanupDestroyed）で呼ぶこと。
    void ReleaseRetired();

    // ===== アクセス =====

    bool   IsEmpty() const { return colliders_.empty(); }
    size_t Count() const { return colliders_.size(); }

    /// @brief 先頭のコライダー（無ければ nullptr）
    Collider* GetFirst();
    /// @brief 先頭のコライダーを取得する（1 本も無ければ nullptr）
    const Collider* GetFirst() const;

    /// @brief インデックス指定（範囲外なら nullptr）
    Collider* Get(size_t index);
    /// @brief index 番目のコライダーを取得する（範囲外は nullptr）
    const Collider* Get(size_t index) const;

    /// @brief 有効なコライダーを順に処理する
    /// @param fn void(Collider&) を受け取る呼び出し可能オブジェクト
    template <class Fn>
    void ForEachEnabled(Fn&& fn) {
        for (auto& collider : colliders_) {
            if (collider && collider->IsEnabled()) {
                fn(*collider);
            }
        }
    }

private:
    CollisionCallback onEnter_;
    CollisionCallback onStay_;
    CollisionCallback onExit_;

    /// 生きているコライダー（実体は個別確保 = 参照が安定）
    std::vector<std::unique_ptr<Collider>> colliders_;

    /// 取り外し済みコライダーの墓場（フレーム末まで実体を保持する）
    std::vector<std::unique_ptr<Collider>> retired_;
};
}
