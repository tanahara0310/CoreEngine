/// @file GodRayCommon.hlsli
/// @brief ゴッドレイ（雲の隙間の光芒）の共通定義
/// @details C++ 側 GodRayShaderConstants（128 バイト）と一致させること。
///          設計書: Docs/Engine/Graphics/Rendering/GodRay_Design.md

#ifndef GODRAY_COMMON_HLSLI
#define GODRAY_COMMON_HLSLI

struct GodRayConstants
{
    float4x4 invViewProj;                                       // 0
    float3 cameraWorldPos;      float maxDistanceM;             // 64
    float shadowRegionCenterX;  float shadowRegionCenterZ;
    float shadowRegionSizeM;    float shadowAnchorWorldY;       // 80
    float intensity;            float mieBoost;
    float groundLevelY;         float edgeFadeStart;            // 96
    uint stepCount;             uint outputWidth;
    uint outputHeight;          uint pad0;                      // 112 (= 128)
};

// C++ 側 VolumetricCloudManager::kCloudShadowMapSize と一致させること
static const uint CLOUD_SHADOW_MAP_SIZE = 1024;

/// @brief 雲シャドウマップから点 P の太陽方向の雲透過率を取得する
/// @param P ワールド座標 [m]（雲底面より下の点を想定）
/// @param toSun 太陽の位置方向（正規化）
/// @details マップは「雲底面（shadowAnchorWorldY）上の XZ」でパラメータ化されている。
///          P から太陽方向へ雲底面まで進んだ交点の XZ でサンプルする。
///          toSun.y の下限クランプは太陽が低いときの射影距離の発散防止（v1 の既知近似）。
///          マップ範囲外は「遮蔽なし（1.0）」へフェードする。
float SampleCloudShadow(Texture2D<float> shadowMap, SamplerState clampSamp, float3 P,
                        float3 toSun, GodRayConstants gr)
{
    float t = (gr.shadowAnchorWorldY - P.y) / max(toSun.y, 0.08f);
    float2 anchorXZ = P.xz + toSun.xz * max(t, 0.0f);
    float2 uv = (anchorXZ - float2(gr.shadowRegionCenterX, gr.shadowRegionCenterZ))
              / gr.shadowRegionSizeM + 0.5f;

    float shadow = shadowMap.SampleLevel(clampSamp, uv, 0);

    // マップ端フェード（正規化半径 edgeFadeStart→1.0 で遮蔽なしへ）
    float r = max(abs(uv.x - 0.5f), abs(uv.y - 0.5f)) * 2.0f;
    return lerp(shadow, 1.0f, smoothstep(gr.edgeFadeStart, 1.0f, r));
}

#endif // GODRAY_COMMON_HLSLI
