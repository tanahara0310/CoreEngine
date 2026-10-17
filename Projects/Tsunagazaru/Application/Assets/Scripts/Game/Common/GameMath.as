using namespace CoreEngine;

const float kPi = 3.14159265f;
const float kTau = 6.28318531f;

// マスごとに決まる 0〜1 の値（同じマスなら何フレーム後でも同じ値）
float HashCell01(int x, int y)
{
    uint value = uint(x) * uint(73856093) ^ uint(y) * uint(19349663);
    value ^= value >> 16;
    value *= uint(0x7FEB352D);
    value ^= value >> 15;
    value *= uint(0x846CA68B);
    value ^= value >> 16;
    return float(value >> 8) * (1.0f / 16777216.0f);
}

// 角度の補間（差を -π〜π に丸めて近い向きへ回す）
float LerpAngle(float startAngle, float endAngle, float t, EaseType ease)
{
    const float easedT = Ease(ease, t);
    float diff = endAngle - startAngle;
    while (diff > kPi) {
        diff -= kTau;
    }
    while (diff < -kPi) {
        diff += kTau;
    }
    return startAngle + diff * easedT;
}

float MoveTowards(float current, float target, float maxDelta)
{
    if (abs(target - current) <= maxDelta) {
        return target;
    }
    return current + (target > current ? maxDelta : -maxDelta);
}

float Smoothstep(float edge0, float edge1, float x)
{
    const float t = Saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

// 1 マス幅 1.6・底面 Y = 0 で作ったブロックのモデルを、1 マスへ置くための寸法
namespace BlockLayout
{
    const float kModelBlockSize = 1.6f;
    const float kRailModelHeight = 0.2f;
    const float kBridgeModelHeight = 0.2f;
    const float kStationModelHeight = 3.0f;
    // ground.obj の草の帯が始まるモデル内の高さ
    const float kGroundGrassStartY = 1.0f;

    float GetScale(float gridSize) { return gridSize / kModelBlockSize; }
    float GetGroundHeight(float gridSize) { return -gridSize * 0.5f; }
    float GetSurfaceHeight(float gridSize) { return GetGroundHeight(gridSize) + kModelBlockSize * GetScale(gridSize); }
    // 橋の上面と地面の上面を揃える
    float GetBridgeHeight(float gridSize) { return GetSurfaceHeight(gridSize) - kBridgeModelHeight * GetScale(gridSize); }
    float GetRailTopHeight(float gridSize) { return GetSurfaceHeight(gridSize) + kRailModelHeight * GetScale(gridSize); }
    // 地面ブロックの下へ逆さに吊るす柱の Y の拡大率
    float GetGroundSkirtYScale(float gridSize, float skirtHeight) { return GetScale(gridSize) * (skirtHeight / gridSize); }
}
