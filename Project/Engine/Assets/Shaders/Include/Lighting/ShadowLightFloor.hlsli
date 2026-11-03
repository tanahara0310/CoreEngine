// ============================================================
// 影の中に残す光の割合
// 不透明面・水面・水中・RT の当たった点の影で同じ値を使う。
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
