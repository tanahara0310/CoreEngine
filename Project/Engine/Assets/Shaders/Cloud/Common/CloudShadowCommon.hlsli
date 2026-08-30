/// @file CloudShadowCommon.hlsli
/// @brief 雲シャドウマップのパラメータ化とサンプル
/// @details C++ 側 CloudShadowShaderConstants（32 バイト）と一致させること。
///          生成 CS・ゴッドレイ・Deferred ライティングの 3 者がこのヘッダを共有する。

#ifndef CLOUD_SHADOW_COMMON_HLSLI
#define CLOUD_SHADOW_COMMON_HLSLI

struct CloudShadowConstants
{
    float regionCenterX;    float regionCenterZ;
    float regionSizeM;      float anchorWorldY;     // 0
    float edgeFadeStart;    float sceneStrength;
    float pad0;             float pad1;             // 16 (= 32)
};

// C++ 側 CloudResources::kCloudShadowMapSize と一致させること
static const uint CLOUD_SHADOW_MAP_SIZE = 1024;

/// @brief 雲シャドウマップから点 P の太陽方向の雲透過率を取得する
/// @param P ワールド座標 [m]（雲底面より下の点を想定）
/// @param toSun 太陽の位置方向（正規化）
/// @details マップは「雲底面（anchorWorldY）上の XZ」でパラメータ化されている。
///          P から太陽方向へ雲底面まで進んだ交点の XZ でサンプルする。
///          toSun.y の下限クランプは太陽が低いときの射影距離の発散防止。
///          マップ範囲外は「遮蔽なし（1.0）」へフェードする。
float SampleCloudShadow(Texture2D<float> shadowMap, SamplerState clampSamp, float3 P,
                        float3 toSun, CloudShadowConstants cs)
{
    float t = (cs.anchorWorldY - P.y) / max(toSun.y, 0.08f);
    float2 anchorXZ = P.xz + toSun.xz * max(t, 0.0f);
    float2 uv = (anchorXZ - float2(cs.regionCenterX, cs.regionCenterZ)) / cs.regionSizeM + 0.5f;

    float shadow = shadowMap.SampleLevel(clampSamp, uv, 0);

    // マップ端フェード（正規化半径 edgeFadeStart→1.0 で遮蔽なしへ）
    float r = max(abs(uv.x - 0.5f), abs(uv.y - 0.5f)) * 2.0f;
    return lerp(shadow, 1.0f, smoothstep(cs.edgeFadeStart, 1.0f, r));
}

#endif // CLOUD_SHADOW_COMMON_HLSLI
