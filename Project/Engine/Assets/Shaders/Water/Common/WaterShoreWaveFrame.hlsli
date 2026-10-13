// ============================================================
// 水面のラスタ描画（FFTWater.VS / Water.PS）が読む岸の波
// ------------------------------------------------------------
// WaterFrameConstants(b5) の値から岸の波の設定を組み立てる。場はレジスタ t29（VS と PS で同じ）
// ============================================================
#ifndef WATER_SHORE_WAVE_FRAME_INCLUDED
#define WATER_SHORE_WAVE_FRAME_INCLUDED

#include "WaterFrameConstants.hlsli"
#include "WaterShoreWave.hlsli"

// gShoreWaveEnabled が 1 のフレームだけ読む
Texture2D<float4> gShoreWaveField : register(t29);

/// @brief b5 の岸の波の設定
ShoreWaveParams GetFrameShoreWaveParams()
{
    ShoreWaveParams p;
    p.enabled = gShoreWaveEnabled != 0 && gShoreMeshEnabled != 0;
    p.originXZ = gSeabedOriginXZ;
    p.size = gSeabedInvSize > 0.0f ? 1.0f / gSeabedInvSize : 0.0f;
    p.edgeMargin = gShoreWaveEdgeMargin;
    p.angularFrequency = gShoreWaveAngularFrequency;
    p.height = gShoreWaveHeight;
    p.startDepth = gShoreWaveStartDepth;
    p.heightVariation = gShoreWaveHeightVariation;
    return p;
}

/// @brief 変位前の位置 baseXZ の今のフレームの岸の波
ShoreWaveResult EvaluateFrameShoreWave(float2 baseXZ)
{
    return EvaluateShoreWave(gShoreWaveField, GetFrameShoreWaveParams(), baseXZ, gShoreWavePhaseTime);
}

#endif // WATER_SHORE_WAVE_FRAME_INCLUDED
