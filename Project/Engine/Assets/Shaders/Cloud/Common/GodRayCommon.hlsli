/// @file GodRayCommon.hlsli
/// @brief ゴッドレイ（雲の隙間の光芒）の共通定義
/// @details C++ 側 GodRayShaderConstants（112 バイト）と一致させること。
///          雲シャドウマップの参照は CloudShadowCommon.hlsli が持つ。

#ifndef GODRAY_COMMON_HLSLI
#define GODRAY_COMMON_HLSLI

struct GodRayConstants
{
    float4x4 invViewProj;                                       // 0
    float3 cameraWorldPos;      float maxDistanceM;             // 64
    float intensity;            float mieBoost;
    float groundLevelY;         float pad1;                     // 80
    uint stepCount;             uint outputWidth;
    uint outputHeight;          uint pad0;                      // 96 (= 112)
};

#endif // GODRAY_COMMON_HLSLI
