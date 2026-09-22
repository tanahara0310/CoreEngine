#pragma once

#include "CollisionInfo.h"
#include "CollisionLayer.h"
#include "CollisionShape.h"
#include "Math/Geometry/Intersect.h"

#include <cstdint>

namespace CoreEngine
{
class GameObject;

/// @brief 衝突判定の 1 単位（仮想関数を持たないデータクラス）
/// @details 形状は CollisionShape のタグで持ち、判定は Math/Geometry へディスパッチ表経由で委譲する。
/// @note 所有権は GameObject（ColliderComponent）側。CollisionWorld は借用するだけ。
class Collider {
public:
    Collider();
    Collider(GameObject* owner, const CollisionShape& shape, CollisionLayer layer);

    /// @brief このコライダーの一意 ID
    /// @details 生成順に振られ再利用されない。衝突履歴のキーに使っても ABA 問題が起きない。
    uint64_t GetId() const { return id_; }

    // ===== 判定 =====

    /// @brief 他のコライダーと交差しているか
    /// @param other 相手
    /// @param outContact 省略可。接触情報（法線は this から other へ向かう）
    bool Intersects(const Collider& other, Geometry::Contact* outContact = nullptr) const;

    // ===== ワールド空間の形状 =====

    /// @brief ワールド空間の中心座標（オフセットとスケール適用後）
    Vector3 GetWorldCenter() const;

    /// @brief ワールド空間の球（type != Sphere でも外接球として使える）
    Geometry::Sphere GetWorldSphere() const;

    /// @brief ワールド空間の AABB（type != Box でも外接 AABB として使える）
    /// @note ボックスが回転している場合は、回転後の箱に外接する AABB を返す。
    Geometry::AABB GetWorldAABB() const;

    /// @brief ワールド空間の向き付きボックス（type != Box でも外接ボックスとして使える）
    Geometry::OBB GetWorldOBB() const;

    /// @brief ワールド空間のカプセル（type != Capsule でも外接カプセルとして使える）
    /// @note 軸はオーナーの上方向。半径は最大スケール、全高は上方向のスケールが乗る。
    Geometry::Capsule GetWorldCapsule() const;

    /// @brief オーナーのワールドスケール（未設定なら等倍）
    Vector3 GetWorldScale() const;

    // ===== 形状 =====

    const CollisionShape& GetShape() const { return shape_; }
    void SetShape(const CollisionShape& shape) { shape_ = shape; }
    ColliderShapeType GetShapeType() const { return shape_.type; }

    /// @brief 球とカプセルの半径を変更する（Box 形状には無効）
    void SetRadius(float radius);
    /// @brief ボックスのサイズを変更する（Sphere / Capsule 形状には無効）
    void SetSize(const Vector3& size);
    /// @brief カプセルの全高を変更する（他の形状には無効）
    void SetHeight(float height);
    /// @brief ローカルオフセットを変更する
    void SetOffset(const Vector3& offset) { shape_.offset = offset; }

    // ===== プロパティ =====

    void SetLayer(CollisionLayer layer) { layer_ = layer; }
    CollisionLayer GetLayer() const { return layer_; }

    void SetOwner(GameObject* owner) { owner_ = owner; }
    GameObject* GetOwner() const { return owner_; }

    void SetEnabled(bool enabled) { isEnabled_ = enabled; }
    bool IsEnabled() const { return isEnabled_; }

    /// @brief 通知専用か（true = めり込みを解消しない）
    /// @note 現状 3D 側に押し出しは無いので常にトリガー相当。Phase 4 で使う。
    void SetTrigger(bool isTrigger) { isTrigger_ = isTrigger; }
    bool IsTrigger() const { return isTrigger_; }

    /// @brief 動かないコライダーか
    /// @note Phase 5 のブロードフェーズが再挿入を省略するために使う。
    void SetStatic(bool isStatic) { isStatic_ = isStatic; }
    bool IsStatic() const { return isStatic_; }

    /// @brief このフレームでぶつかった強さ（N・s）
    /// @note 物理が毎フレーム書き込む。衝突の通知へそのまま渡す。
    float GetLastImpulse() const { return lastImpulse_; }

    /// @brief ぶつかった強さを記録する（大きい方を残す）
    void AccumulateImpulse(float impulse)
    {
        if (impulse > lastImpulse_) { lastImpulse_ = impulse; }
    }

    /// @brief 記録した強さを消す
    void ClearImpulse() { lastImpulse_ = 0.0f; }

    /// @brief 物理が扱うコライダーか
    /// @note 物理側が毎フレーム立てる。立っているものが絡む接触は、めり込みの解消を物理へ任せる。
    void SetSimulated(bool isSimulated) { isSimulated_ = isSimulated; }
    bool IsSimulated() const { return isSimulated_; }

    // ===== 衝突イベント（オーナーへ転送） =====

    /// @param contact 接触情報。normal は this から other へ向かう向き。
    void OnCollisionEnter(Collider* other, const Geometry::Contact& contact);
    /// @brief 接触が続いている間、毎フレーム呼ばれる
    void OnCollisionStay(Collider* other, const Geometry::Contact& contact);

    /// @note Exit は接触が切れた後なので接触情報を持たない
    void OnCollisionExit(Collider* other);

private:
    uint64_t       id_ = 0;
    CollisionShape shape_{};
    GameObject*    owner_ = nullptr;
    CollisionLayer layer_ = CollisionLayer::Default;
    bool           isEnabled_ = true;
    bool           isTrigger_ = true;
    bool           isStatic_ = false;
    bool           isSimulated_ = false;
    float          lastImpulse_ = 0.0f;
};
}
