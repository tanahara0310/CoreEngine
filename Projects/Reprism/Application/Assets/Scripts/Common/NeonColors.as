using namespace CoreEngine;

// 元のゲームの色（線形の値。スプライトと UI へ渡すときは Td3LinearToSrgb を通す）

const Vector4 kNeonWarpCore  = Vector4(0.005f, 0.0f, 0.018f, 1.0f);
const Vector4 kNeonWarpInner = Vector4(0.23f, 0.05f, 0.8f, 1.0f);
const Vector4 kNeonWarpOuter = Vector4(0.0f, 0.95f, 1.0f, 1.0f);

// 自機（#15B800）・ハット（#FF7A00）・ブロック（#FF0099）
const Vector4 kNeonPlayerColor        = Vector4(0.082f, 0.722f, 0.0f, 1.0f);
const Vector4 kNeonPlayerDark         = Vector4(0.02f, 0.16f, 0.0f, 0.5f);
const Vector4 kNeonPlayerGlow         = Vector4(0.082f, 0.722f, 0.0f, 1.0f);
const Vector4 kNeonPlayerInactive     = Vector4(0.02f, 0.11f, 0.0f, 0.7f);
const Vector4 kNeonPlayerGlowInactive = Vector4(0.14f, 0.45f, 0.0f, 0.85f);
const Vector3 kNeonPlayerColorRGB     = Vector3(0.082f, 0.722f, 0.0f);

const Vector4 kNeonHatColor        = Vector4(1.0f, 0.478f, 0.0f, 1.0f);
const Vector4 kNeonHatDark         = Vector4(0.3f, 0.1f, 0.0f, 0.5f);
const Vector4 kNeonHatGlow         = Vector4(1.0f, 0.478f, 0.0f, 1.0f);
const Vector4 kNeonHatInactive     = Vector4(0.22f, 0.08f, 0.0f, 0.7f);
const Vector4 kNeonHatGlowInactive = Vector4(0.6f, 0.25f, 0.0f, 0.85f);
const Vector3 kNeonHatColorRGB     = Vector3(1.0f, 0.478f, 0.0f);

const Vector4 kNeonBlockColor    = Vector4(1.0f, 0.0f, 0.6f, 1.0f);
const Vector3 kNeonBlockColorRGB = Vector3(1.0f, 0.0f, 0.6f);

// 遊びの粒の色（自機・ハット・ブロックのどれか）
Vector4 PickGameplayParticleColor()
{
    switch (Random::Range(0, 2)) {
    case 0:
        return kNeonPlayerColor;
    case 1:
        return kNeonHatColor;
    }
    return kNeonBlockColor;
}
