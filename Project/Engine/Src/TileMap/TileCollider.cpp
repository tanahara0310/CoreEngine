#include "pch.h"
#include "TileCollider.h"
#include "Math/Geometry/Intersect.h"
#include <algorithm>
#include <cmath>

namespace CoreEngine
{
    // ─────────────────────────────────────────────────────────────────────
    // 設定
    // ─────────────────────────────────────────────────────────────────────
    void TileCollider::SetTileMap(const TileMap* tileMap, int layerIndex)
    {
        tileMap_    = tileMap;
        layerIndex_ = layerIndex;
    }

    void TileCollider::SetHalfSize(float halfWidth, float halfHeight)
    {
        halfW_ = halfWidth;
        halfH_ = halfHeight;
    }

    // ─────────────────────────────────────────────────────────────────────
    // 衝突解決
    //
    //  X軸・Y軸を完全に独立して処理することでコーナー誤判定を防ぐ
    //
    //  処理順:
    //   1. X軸のみ移動 → X方向のSolidタイルとの重なりを解決
    //   2. Y軸のみ移動 → Y方向のSolidタイルとの重なりを解決
    //
    //  Camera2D 座標系（Y軸上正）:
    //   接地  = キャラ底面がタイル上面に接触 (isOnGround)
    //   天井  = キャラ上面がタイル底面に接触 (hitCeiling)
    //   左壁  = キャラ左面がタイル右面に接触 (hitWallLeft)
    //   右壁  = キャラ右面がタイル左面に接触 (hitWallRight)
    // ─────────────────────────────────────────────────────────────────────
    TileCollisionResult TileCollider::Resolve(Vector2 oldPosition, Vector2 velocity) const
    {
        TileCollisionResult result;
        result.resolvedPosition = oldPosition;
        result.velocity         = velocity;

        if (!tileMap_) { return result; }

        const float ts = tileMap_->GetTileSize();
        if (ts <= 0.0f) { return result; }

        // ── X 軸のみ移動してX方向を解決 ─────────────────────────────
        result.resolvedPosition.x = oldPosition.x + velocity.x;

        {
            int colMin, colMax, rowMin, rowMax;
            GetOverlappingTileRange(result.resolvedPosition, colMin, colMax, rowMin, rowMax);

            for (int row = rowMin; row <= rowMax; ++row) {
                for (int col = colMin; col <= colMax; ++col) {
                    if (!tileMap_->IsSolid(layerIndex_, col, row)) { continue; }

                    Vector2 tileTopLeft = tileMap_->TileToWorldTopLeft(col, row);
                    float tileLeft   = tileTopLeft.x;
                    float tileRight  = tileTopLeft.x + ts;
                    float tileTop    = tileTopLeft.y;
                    float tileBottom = tileTopLeft.y - ts;

                    float charLeft   = result.resolvedPosition.x - halfW_;
                    float charRight  = result.resolvedPosition.x + halfW_;
                    float charTop    = result.resolvedPosition.y + halfH_;
                    float charBottom = result.resolvedPosition.y - halfH_;

                    // Y方向に重なっているか（わずかな誤差を許容）
                    if (charTop  <= tileBottom || charBottom >= tileTop)  { continue; }
                    // X方向に重なっているか
                    if (charRight <= tileLeft  || charLeft  >= tileRight) { continue; }

                    // 押し出し量の計算式は Geometry と共有する（2 箇所に散らさない）
                    Geometry::AxisOverlap overlap{};
                    if (!Geometry::OverlapOnAxis(charLeft, charRight, tileLeft, tileRight, overlap)) {
                        continue;
                    }

                    result.resolvedPosition.x += overlap.depth * overlap.direction;
                    result.velocity.x = 0.0f;
                    if (overlap.direction < 0.0f) {
                        result.hitWallRight = true;   // 左へ押し出された = 右壁に当たった
                    } else {
                        result.hitWallLeft = true;
                    }
                }
            }
        }

        // ── Y 軸のみ移動してY方向を解決 ─────────────────────────────
        result.resolvedPosition.y = oldPosition.y + velocity.y;

        {
            int colMin, colMax, rowMin, rowMax;
            GetOverlappingTileRange(result.resolvedPosition, colMin, colMax, rowMin, rowMax);

            for (int row = rowMin; row <= rowMax; ++row) {
                for (int col = colMin; col <= colMax; ++col) {
                    if (!tileMap_->IsSolid(layerIndex_, col, row)) { continue; }

                    Vector2 tileTopLeft = tileMap_->TileToWorldTopLeft(col, row);
                    float tileLeft   = tileTopLeft.x;
                    float tileRight  = tileTopLeft.x + ts;
                    float tileTop    = tileTopLeft.y;
                    float tileBottom = tileTopLeft.y - ts;

                    // X解決済みのX座標でチェック
                    float charLeft   = result.resolvedPosition.x - halfW_;
                    float charRight  = result.resolvedPosition.x + halfW_;
                    float charTop    = result.resolvedPosition.y + halfH_;
                    float charBottom = result.resolvedPosition.y - halfH_;

                    // X方向に重なっているか
                    if (charRight <= tileLeft  || charLeft  >= tileRight) { continue; }
                    // Y方向に重なっているか
                    if (charTop  <= tileBottom || charBottom >= tileTop)  { continue; }

                    // 押し出し量の計算式は Geometry と共有する（2 箇所に散らさない）
                    Geometry::AxisOverlap overlap{};
                    if (!Geometry::OverlapOnAxis(charBottom, charTop, tileBottom, tileTop, overlap)) {
                        continue;
                    }

                    result.resolvedPosition.y += overlap.depth * overlap.direction;
                    result.velocity.y = 0.0f;
                    if (overlap.direction > 0.0f) {
                        result.isOnGround = true;   // 上へ押し出された = 上から踏んでいる
                    } else {
                        result.hitCeiling = true;
                    }
                }
            }
        }

        return result;
    }

    // ─────────────────────────────────────────────────────────────────────
    // AABB が重なるタイル範囲を計算
    // ─────────────────────────────────────────────────────────────────────
    void TileCollider::GetOverlappingTileRange(
        Vector2 center,
        int& colMin, int& colMax,
        int& rowMin, int& rowMax) const
    {
        const float ts     = tileMap_->GetTileSize();
        const Vector2 origin = tileMap_->GetOrigin();

        float left   = center.x - halfW_;
        float right  = center.x + halfW_;
        float top    = center.y + halfH_;
        float bottom = center.y - halfH_;

        colMin = static_cast<int>(std::floor((left   - origin.x) / ts));
        colMax = static_cast<int>(std::floor((right  - origin.x) / ts));
        rowMin = static_cast<int>(std::floor((origin.y - top)    / ts));
        rowMax = static_cast<int>(std::floor((origin.y - bottom) / ts));

        colMin = std::max(colMin, 0);
        colMax = std::min(colMax, tileMap_->GetCols() - 1);
        rowMin = std::max(rowMin, 0);
        rowMax = std::min(rowMax, tileMap_->GetRows() - 1);
    }

} // namespace CoreEngine

