#pragma once

#include "Math/Geometry/Intersect.h"

namespace CoreEngine
{
class Collider;

/// @brief めり込みの解消（押し出し）
/// @details トリガー（通知専用）でないコライダー同士が重なったとき、
///          最小移動量で引き離す。
///
///          **現状は単発の解決**（反復ソルバではない）。同一フレーム内で
///          複数のペアを順に解くため、3 つ以上が同時に押し合う状況では
///          1 フレームで完全には収束しない。壁 1 枚に当たって止まる、という
///          典型ケースには十分。
namespace CollisionResolver
{
    /// @brief 1 ペアのめり込みを解消する
    /// @param a 片方（contact.normal は a から b へ向かう）
    /// @param b もう片方
    /// @param contact a×b の接触情報
    /// @return 実際に押し出したら true
    /// @note どちらかが Trigger なら何もしない。
    ///       Static もしくは押し出しを受け付けないオブジェクトは動かさず、
    ///       もう一方を全量押し出す。両方とも動かせないなら何もしない。
    bool Resolve(Collider& a, Collider& b, const Geometry::Contact& contact);
}
}
