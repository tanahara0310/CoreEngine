#pragma once

// G-Buffer の WorldPosition ターゲット削除に伴う深度ベースのワールド座標復元ヘルパー。
// 標準Z（near=0, far=1, クリア値=1.0）を前提とする。
// GodRayMarch.CS.hlsl / AerialPerspective.CS.hlsl と同じ変換式・NDC規約に合わせること。

/// @brief スクリーンUV（原点左上・Y下向き）からNDC座標（Y上向き）へ変換する
float2 ScreenUVToNDC(float2 uv)
{
    return float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
}

/// @brief NDC深度が背景（未描画ピクセル）かどうかを判定する
bool IsBackgroundDepth(float ndcDepth)
{
    return ndcDepth >= 0.9999999f;
}

/// @brief NDC座標・深度・View*Projectionの逆行列からワールド座標を復元する
/// @param ndc NDC xy（x:[-1,1] 右方向, y:[-1,1] 上方向）
/// @param ndcDepth 深度バッファの値（[0,1]、near=0/far=1）
/// @param invViewProj View*Projection の逆行列（行ベクトル規約: p' = p * M）
float3 ReconstructWorldPosition(float2 ndc, float ndcDepth, float4x4 invViewProj)
{
    float4 wp = mul(float4(ndc, ndcDepth, 1.0f), invViewProj);
    return wp.xyz / wp.w;
}
