#pragma once

#include "Math/Geometry/Intersect.h"

namespace CoreEngine
{
class Collider;

/// @brief めり込みの解消（押し出し）
/// @details トリガーでないコライダー同士が重なったとき、最小移動量で引き離す。
/// @note 反復ソルバではない。3 つ以上が同時に押し合う状況では 1 フレームで収束しない。
namespace CollisionResolver
{
    /// @brief 1 ペアのめり込みを解消する（contact.normal は a から b へ向かう）
    /// @return 実際に押し出したら true
    /// @note Trigger なら何もしない。動かせない側があればもう一方を全量押し出す。
    bool Resolve(Collider& a, Collider& b, const Geometry::Contact& contact);
}
}
