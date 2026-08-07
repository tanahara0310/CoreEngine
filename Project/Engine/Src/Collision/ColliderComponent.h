#pragma once

#include "Collider.h"

#include <memory>
#include <vector>

namespace CoreEngine
{
class GameObject;

/// @brief 1 つの GameObject が持つコライダーの集合
/// @details 以前は GameObject が `unique_ptr<Collider>` を 1 本だけ持っていたため、
///          本体判定と攻撃判定を別レイヤーで同時に持つことができなかった
///          （CollisionLayer に Boss と BossAttack が両方あるのに片方しか使えない状態）。
///
///          **参照の安定性**: Collider は個別にヒープへ確保するので、要素を追加しても
///          既存の Collider& / Collider* は無効化されない。Add() が参照を返す API を
///          安全に保つための要件。
///
///          **解放の遅延**: Remove 系は実体を即 delete せず retired_ へ退避する。
///          衝突コールバックの中から着脱されても、判定ループが持っている生ポインタが
///          宙に浮かない。実体の解放はフレーム末の ReleaseRetired()（GameObjectManager
///          が衝突判定より後に呼ぶ）で行う。
class ColliderComponent {
public:
    /// @brief 所有者を設定する（GameObject の初期化時に一度だけ）
    void SetOwner(GameObject* owner) { owner_ = owner; }

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
    const Collider* GetFirst() const;

    /// @brief インデックス指定（範囲外なら nullptr）
    Collider* Get(size_t index);
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
    GameObject* owner_ = nullptr;

    /// 生きているコライダー（実体は個別確保 = 参照が安定）
    std::vector<std::unique_ptr<Collider>> colliders_;

    /// 取り外し済みコライダーの墓場（フレーム末まで実体を保持する）
    std::vector<std::unique_ptr<Collider>> retired_;
};
}
