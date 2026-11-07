// ============================================================
// 影の中に残す光の割合
// 空の環境光が無いときの代わりの光（半球ランバートの環境光・前方描画の拡散）だけに使う。
// 太陽の直接光と空の環境光には使わない。
// ============================================================
#ifndef SHADOW_LIGHT_FLOOR_INCLUDED
#define SHADOW_LIGHT_FLOOR_INCLUDED

/// @brief 影の中に残す光の割合
static const float kShadowLightFloor = 0.3f;

/// @brief 日向率（0=影 / 1=日向）を、影の中にも kShadowLightFloor だけ光を残す倍率へ変える
float ApplyShadowLightFloor(float visibility)
{
    return lerp(kShadowLightFloor, 1.0f, visibility);
}

#endif // SHADOW_LIGHT_FLOOR_INCLUDED
