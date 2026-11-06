/// @file SkinnedMotion.hlsli
/// @brief スキンのモデルの前のフレームの位置（Skinning.CS.hlsl が 1 フレーム前の姿勢で変形して書く）

#ifndef SKINNED_MOTION_HLSLI
#define SKINNED_MOTION_HLSLI

/// @brief 1 フレーム前の姿勢で変形した物体の空間の位置（頂点の番号で引く）
StructuredBuffer<float4> gPrevSkinnedPositions : register(t2, space1);

/// @brief 1 フレーム前の物体の空間の位置
float4 LoadPrevSkinnedPosition(uint vertexID)
{
    return gPrevSkinnedPositions[vertexID];
}

#endif // SKINNED_MOTION_HLSLI
