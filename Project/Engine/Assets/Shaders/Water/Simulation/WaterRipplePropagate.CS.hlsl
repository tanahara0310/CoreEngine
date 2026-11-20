// ============================================================
// 水の波紋を 1 歩進める
// 深い水の重力波（ω² = g|k|）を、√(-∇²) を近似する半径 8 マスの円い重み K で進める。
//   h_{n+1} = h_n + keep × (h_n - h_{n-1}) - (g Δt² / Δx) × (K ⊛ h_n)
// K の周波数の応答は波長 2.5〜16 マスで |k| と ±1% で一致し、どの波数でも 0 以上 4.24 以下。
// 格子の最小の角を動かした歩は、古い状態を gShiftCells だけずらして読む（範囲の外は 0）。
// 端から kRippleSpongeCells マスの帯では h_{n+1} と h_n を弱めて、外へ出る波を吸い込む。
// 水面が読む面へは h_n とその傾きを書く。
// ============================================================

#include "../Common/WaterRippleGrid.hlsli"

cbuffer WaterRipplePropagateConstants : register(b0)
{
    int2 gShiftCells;        // 新しい最小の角 - 古い最小の角 [マス]
    uint gPropagateLevel;
    uint gReadPair;          // 読む組（書くのは 1 - gReadPair）
    float gCourant;          // g Δt² / Δx
    float gKeep;             // 1 歩で速さを残す割合
    float gInvTwoCellSize;   // 1 / (2 × 1 マスの幅) [1/m]
    uint gClear;             // 1 = 書く組と面を 0 にする
};

RWTexture2DArray<float> gRippleState : register(u0);
RWTexture2DArray<float4> gRippleSurface : register(u1);

static const int kKernelRadius = 8;
static const int kTileSize = 16;
static const int kTileSpan = kTileSize + 2 * kKernelRadius;
// 帯のいちばん外側で 1 歩ごとに弱める割合
static const float kSpongeStrength = 0.15f;

groupshared float sHeight[kTileSpan][kTileSpan];

float LoadState(int2 cell, uint slice)
{
    if (any(cell < 0) || any(cell >= int(kRippleResolution)))
    {
        return 0.0f;
    }
    return gRippleState[uint3(cell, slice)];
}

[numthreads(16, 16, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID, uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint readHeight = RippleStateSlice(gPropagateLevel, gReadPair, 0);
    const uint readPrevious = RippleStateSlice(gPropagateLevel, gReadPair, 1);
    const uint writePair = 1 - gReadPair;

    // 16×16 のマスと周り 8 マスの h_n を共有メモリへ読む
    const int2 tileOrigin = int2(groupId.xy) * kTileSize - kKernelRadius;
    [unroll]
    for (int k = 0; k < 4; ++k)
    {
        const int2 local = int2(groupThreadId.xy) + kTileSize * int2(k & 1, k >> 1);
        sHeight[local.y][local.x] = (gClear != 0) ? 0.0f : LoadState(tileOrigin + local + gShiftCells, readHeight);
    }
    GroupMemoryBarrierWithGroupSync();

    const int2 cell = int2(dispatchThreadId.xy);
    const uint3 writeHeightTexel = uint3(cell, RippleStateSlice(gPropagateLevel, writePair, 0));
    const uint3 writePreviousTexel = uint3(cell, RippleStateSlice(gPropagateLevel, writePair, 1));
    const uint3 surfaceTexel = uint3(cell, gPropagateLevel);
    if (gClear != 0)
    {
        gRippleState[writeHeightTexel] = 0.0f;
        gRippleState[writePreviousTexel] = 0.0f;
        gRippleSurface[surfaceTexel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const int2 c = int2(groupThreadId.xy) + kKernelRadius;
#define H(dx, dz) sHeight[c.y + (dz)][c.x + (dx)]
    float sum = 0.0f;
    sum += +2.400005206e+00f * (H(0, 0));
    sum += -4.306034764e-01f * (H(-1, 0) + H(0, -1) + H(0, 1) + H(1, 0));
    sum += -8.322152296e-02f * (H(-1, -1) + H(-1, 1) + H(1, -1) + H(1, 1));
    sum += +4.892978739e-02f * (H(-2, 0) + H(0, -2) + H(0, 2) + H(2, 0));
    sum += -9.396735406e-03f * (H(-2, -1) + H(-2, 1) + H(-1, -2) + H(-1, 2) + H(1, -2) + H(1, 2) + H(2, -1) + H(2, 1));
    sum += -5.929617810e-03f * (H(-2, -2) + H(-2, 2) + H(2, -2) + H(2, 2));
    sum += -3.664868431e-02f * (H(-3, 0) + H(0, -3) + H(0, 3) + H(3, 0));
    sum += -5.292913799e-03f * (H(-3, -1) + H(-3, 1) + H(-1, -3) + H(-1, 3) + H(1, -3) + H(1, 3) + H(3, -1) + H(3, 1));
    sum += -4.370516137e-03f * (H(-3, -2) + H(-3, 2) + H(-2, -3) + H(-2, 3) + H(2, -3) + H(2, 3) + H(3, -2) + H(3, 2));
    sum += -6.987892815e-03f * (H(-3, -3) + H(-3, 3) + H(3, -3) + H(3, 3));
    sum += +1.104304038e-02f * (H(-4, 0) + H(0, -4) + H(0, 4) + H(4, 0));
    sum += -2.809114233e-03f * (H(-4, -1) + H(-4, 1) + H(-1, -4) + H(-1, 4) + H(1, -4) + H(1, 4) + H(4, -1) + H(4, 1));
    sum += -1.087168787e-03f * (H(-4, -2) + H(-4, 2) + H(-2, -4) + H(-2, 4) + H(2, -4) + H(2, 4) + H(4, -2) + H(4, 2));
    sum += +2.776056351e-03f * (H(-4, -3) + H(-4, 3) + H(-3, -4) + H(-3, 4) + H(3, -4) + H(3, 4) + H(4, -3) + H(4, 3));
    sum += +2.351743565e-05f * (H(-4, -4) + H(-4, 4) + H(4, -4) + H(4, 4));
    sum += -7.514310629e-03f * (H(-5, 0) + H(0, -5) + H(0, 5) + H(5, 0));
    sum += +1.772477941e-04f * (H(-5, -1) + H(-5, 1) + H(-1, -5) + H(-1, 5) + H(1, -5) + H(1, 5) + H(5, -1) + H(5, 1));
    sum += -3.472461689e-03f * (H(-5, -2) + H(-5, 2) + H(-2, -5) + H(-2, 5) + H(2, -5) + H(2, 5) + H(5, -2) + H(5, 2));
    sum += -3.306588415e-03f * (H(-5, -3) + H(-5, 3) + H(-3, -5) + H(-3, 5) + H(3, -5) + H(3, 5) + H(5, -3) + H(5, 3));
    sum += -6.229664234e-03f * (H(-5, -4) + H(-5, 4) + H(-4, -5) + H(-4, 5) + H(4, -5) + H(4, 5) + H(5, -4) + H(5, 4));
    sum += +1.031086524e-02f * (H(-5, -5) + H(-5, 5) + H(5, -5) + H(5, 5));
    sum += +5.550233982e-04f * (H(-6, 0) + H(0, -6) + H(0, 6) + H(6, 0));
    sum += -2.288858527e-03f * (H(-6, -1) + H(-6, 1) + H(-1, -6) + H(-1, 6) + H(1, -6) + H(1, 6) + H(6, -1) + H(6, 1));
    sum += +8.936415613e-04f * (H(-6, -2) + H(-6, 2) + H(-2, -6) + H(-2, 6) + H(2, -6) + H(2, 6) + H(6, -2) + H(6, 2));
    sum += +3.473476739e-03f * (H(-6, -3) + H(-6, 3) + H(-3, -6) + H(-3, 6) + H(3, -6) + H(3, 6) + H(6, -3) + H(6, 3));
    sum += +1.069592782e-03f * (H(-6, -4) + H(-6, 4) + H(-4, -6) + H(-4, 6) + H(4, -6) + H(4, 6) + H(6, -4) + H(6, 4));
    sum += -7.668025302e-03f * (H(-6, -5) + H(-6, 5) + H(-5, -6) + H(-5, 6) + H(5, -6) + H(5, 6) + H(6, -5) + H(6, 5));
    sum += +5.167012368e-03f * (H(-7, 0) + H(0, -7) + H(0, 7) + H(7, 0));
    sum += -3.956036364e-03f * (H(-7, -1) + H(-7, 1) + H(-1, -7) + H(-1, 7) + H(1, -7) + H(1, 7) + H(7, -1) + H(7, 1));
    sum += +1.468458718e-04f * (H(-7, -2) + H(-7, 2) + H(-2, -7) + H(-2, 7) + H(2, -7) + H(2, 7) + H(7, -2) + H(7, 2));
    sum += -7.548630975e-03f * (H(-7, -3) + H(-7, 3) + H(-3, -7) + H(-3, 7) + H(3, -7) + H(3, 7) + H(7, -3) + H(7, 3));
    sum += -7.345337301e-03f * (H(-8, 0) + H(0, -8) + H(0, 8) + H(8, 0));
    const float height = H(0, 0);
    const float2 gradient = float2(H(1, 0) - H(-1, 0), H(0, 1) - H(0, -1)) * gInvTwoCellSize;
#undef H

    const float previous = LoadState(cell + gShiftCells, readPrevious);
    const float next = height + gKeep * (height - previous) - gCourant * sum;

    const int edgeCells = min(min(cell.x, cell.y), min(int(kRippleResolution) - 1 - cell.x, int(kRippleResolution) - 1 - cell.y));
    const float inside = saturate(1.0f - (float)edgeCells / kRippleSpongeCells);
    const float absorb = 1.0f - kSpongeStrength * inside * inside;

    gRippleState[writeHeightTexel] = next * absorb;
    gRippleState[writePreviousTexel] = height * absorb;
    gRippleSurface[surfaceTexel] = float4(height, gradient, 0.0f);
}
