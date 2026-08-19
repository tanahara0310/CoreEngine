#pragma once

#include "Math/Vector/Vector3.h"

namespace CoreEngine
{
class GameObject;
class Collider;

/// @brief 衝突コールバックへ渡す接触情報（相手・法線・貫通深度・当たったコライダー）
/// @note 符号規約: normal は自分から相手へ向かう向き。自分を -normal 方向へ depth 動かせば接触が解ける。
struct CollisionInfo {
    /// @brief 相手のゲームオブジェクト
    GameObject* other = nullptr;

    /// @brief 自分側のコライダー（1 オブジェクトが複数持つときの識別に使う）
    Collider* selfCollider = nullptr;

    /// @brief 相手側のコライダー
    Collider* otherCollider = nullptr;

    /// @brief 自分 → 相手 の押し出し方向（正規化済み）
    /// @note Exit では接触が既に切れているためゼロベクトル。
    Vector3 normal{ 0.0f, 0.0f, 0.0f };

    /// @brief 貫通深度（0 以上）
    /// @note Exit では 0。
    float depth = 0.0f;

    /// @brief 代表接触点（ワールド座標）
    Vector3 point{ 0.0f, 0.0f, 0.0f };
};
}
