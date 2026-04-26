#pragma once

#include "TileMap.h"
#include "Math/Vector/Vector2.h"

namespace CoreEngine
{
    /// @brief TileCollider::Resolve() の戻り値
    struct TileCollisionResult
    {
        Vector2 resolvedPosition;   ///< 押し出し後のワールド座標（中心）
        Vector2 velocity;           ///< 押し出し後の速度（壁に当たった軸は 0）
        bool    isOnGround = false; ///< 下方向に Solid タイルと接触していれば true
        bool    hitCeiling = false; ///< 上方向に Solid タイルと接触していれば true
        bool    hitWallLeft  = false; ///< 左方向に Solid タイルと接触していれば true
        bool    hitWallRight = false; ///< 右方向に Solid タイルと接触していれば true
    };

    /// @brief キャラクター（AABB）と TileMap の衝突解決クラス
    /// @details
    ///  - 座標系: Camera2D 準拠（中央原点・Y軸上正）
    ///  - キャラクターの AABB 中心座標と半サイズを渡して Resolve() を呼ぶ
    ///  - X 軸・Y 軸を独立して解決（スライド移動に対応）
    class TileCollider
    {
    public:
        /// @brief 使用する TileMap を設定
        /// @param tileMap  衝突判定に使う TileMap
        /// @param layerIndex 判定対象のレイヤーインデックス（省略時 0）
        void SetTileMap(const TileMap* tileMap, int layerIndex = 0);

        /// @brief コライダーの半サイズを設定（ピクセル単位）
        /// @param halfWidth  横半サイズ
        /// @param halfHeight 縦半サイズ
        void SetHalfSize(float halfWidth, float halfHeight);

        /// @brief 衝突解決を実行する
        /// @param oldPosition  移動前のキャラ中心ワールド座標
        /// @param velocity     今フレームの速度（px/frame）
        /// @return 衝突結果（押し出し後座標・速度・接地フラグ等）
        /// @note 内部で「X軸移動→X解決」→「Y軸移動→Y解決」の順で処理する
        TileCollisionResult Resolve(Vector2 oldPosition, Vector2 velocity) const;

        // ===== アクセッサ =====
        const TileMap* GetTileMap()    const { return tileMap_; }
        int            GetLayerIndex() const { return layerIndex_; }
        float          GetHalfWidth()  const { return halfW_; }
        float          GetHalfHeight() const { return halfH_; }

    private:
        /// @brief AABB が重なっているタイル列・行の範囲を計算
        void GetOverlappingTileRange(
            Vector2 center,
            int& colMin, int& colMax,
            int& rowMin, int& rowMax) const;

        const TileMap* tileMap_     = nullptr;
        int            layerIndex_  = 0;
        float          halfW_       = 16.0f;
        float          halfH_       = 16.0f;
    };

} // namespace CoreEngine
