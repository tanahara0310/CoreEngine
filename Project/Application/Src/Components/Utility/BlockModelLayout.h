#pragma once

namespace GameComponents::BlockModelLayout
{
    // 各モデルは XZ 幅 1.6 の共通ブロックを基準に、底面を Y=0 として制作されている。
    // 個々の外接箱に合わせて拡縮せず、制作時の比率を保って1マスへ変換する。
    inline constexpr float kModelBlockSize = 1.6f;
    inline constexpr float kRailModelHeight = 0.2f;
    inline constexpr float kBridgeModelHeight = 0.2f;
    inline constexpr float kStationModelHeight = 3.0f;

    constexpr float GetScale(float gridSize) {
        return gridSize / kModelBlockSize;
    }

    constexpr float GetGroundHeight(float gridSize) {
        return -gridSize * 0.5f;
    }

    constexpr float GetSurfaceHeight(float gridSize) {
        return GetGroundHeight(gridSize) + kModelBlockSize * GetScale(gridSize);
    }

    constexpr float GetBridgeHeight(float gridSize) {
        // 橋の上面と地面の上面を揃え、どちらでも同じ高さにレールを置く。
        return GetSurfaceHeight(gridSize) - kBridgeModelHeight * GetScale(gridSize);
    }

    constexpr float GetRailTopHeight(float gridSize) {
        return GetSurfaceHeight(gridSize) + kRailModelHeight * GetScale(gridSize);
    }

    // ───────── 地面ブロックのスカート ─────────
    // 1マス角のブロックのままだと柱として短すぎて、雲（高さフォグ）に刺さって見えない。
    // そこで地面ブロックの底面から下へ、同じ ground.obj をもう1つ吊り下げて柱を伸ばす。
    // 上面（GetSurfaceHeight）は動かさないので、レール・駅・岩・木・トロッコの高さと
    // マス目の当たり（MapChipType の参照）には一切影響しない。
    //
    // 吊り下げる向きは上下逆さ（X 軸まわりに 180 度）。ground.obj は Y 0.0〜1.2 が土、
    // 1.0〜1.6 が草の 2 色でできていて、そのまま下へ継ぐと草の帯がブロックの真下へ
    // 出てしまう。逆さにすれば草は柱の最下部、つまりフォグが不透明になりきった高さへ回る。

    /// @brief ground.obj の草（緑）が始まるモデル内 Y。ここより下は土（茶）。
    inline constexpr float kGroundGrassStartY = 1.0f;

    /// @brief 逆さに吊るしたスカートで、上端から何割下がると草が出るか
    inline constexpr float kGroundSkirtGrassStartRatio =
        kGroundGrassStartY / kModelBlockSize;

    /// @brief スカートに掛ける Y スケール
    /// @param skirtHeight ブロックの底面から下へ伸ばす長さ [m]
    constexpr float GetGroundSkirtYScale(float gridSize, float skirtHeight) {
        return GetScale(gridSize) * (skirtHeight / gridSize);
    }

    /// @brief スカートの下端（＝見た目の柱の底）の高さ
    constexpr float GetGroundSkirtBottomHeight(float gridSize, float skirtHeight) {
        return GetGroundHeight(gridSize) - skirtHeight;
    }

    /// @brief 逆さに吊るしたスカートの草の帯が始まる高さ
    /// @details この高さより下でフォグが不透明になっていれば、草は画に出ない。
    ///          Game.Fog.* を触るときはここを下回らないか確かめること。
    constexpr float GetGroundSkirtGrassTopHeight(float gridSize, float skirtHeight) {
        return GetGroundHeight(gridSize) - skirtHeight * kGroundSkirtGrassStartRatio;
    }
}
