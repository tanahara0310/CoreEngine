/// @file CloudRayMarch.CS.hlsl
/// @brief 雲の半解像度レイマーチ
/// @details 各ピクセル 1 レイ。球殻交差でマーチ区間を求め、SceneDepth で不透明遮蔽をクランプし、
///          MarchClouds へ渡す。出力は前乗算アルファ形式（rgb=前乗算輝度, a=透過率）。

#include "Common/CloudMarch.hlsli"

Texture2D<float> gSceneDepth : register(t3);
RWTexture2D<float4> gCloudOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint w = gCloud.outputWidth;
    uint h = gCloud.outputHeight;
    if (dtid.x >= w || dtid.y >= h)
    {
        return;
    }

    // ===== レイ生成（半解像度ピクセル → NDC → ワールド方向） =====
    float2 uv = (float2(dtid.xy) + 0.5f) / float2(w, h);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);

    float4 farH = mul(float4(ndc, 1.0f, 1.0f), gCloud.invViewProj);
    farH /= farH.w;
    float3 rayOrigin = gCloud.cameraWorldPos;
    float3 rayDir = normalize(farH.xyz - rayOrigin);

    // ===== マーチ区間（雲層シェル） =====
    float2 interval = CloudLayerInterval(rayOrigin, rayDir, gCloud);
    float marchStart = interval.x;
    float marchEnd = min(interval.y, gCloud.maxMarchDistanceM);

    // ===== 不透明ジオメトリによる遮蔽 =====
    // 出力が半解像度でも動くよう、深度テクスチャの実寸から対応画素を求める
    uint depthW, depthH;
    gSceneDepth.GetDimensions(depthW, depthH);
    int2 fullPix = clamp(int2(uv * float2(depthW, depthH)), int2(0, 0), int2(depthW - 1, depthH - 1));
    float ndcDepth = gSceneDepth.Load(int3(fullPix, 0));
    if (ndcDepth < 0.9999999f)
    {
        float4 wp = mul(float4(ndc, ndcDepth, 1.0f), gCloud.invViewProj);
        wp /= wp.w;
        marchEnd = min(marchEnd, length(wp.xyz - rayOrigin));
    }

    // 画面空間 IGN でサンプル位相をジッタし、等距離サンプル面が作る
    // 放射状のバンディングをノイズへ分散させる
    float ign = InterleavedGradientNoise(float2(dtid.xy));

    CloudMarchResult cloud = MarchClouds(rayOrigin, rayDir, marchStart, marchEnd,
                                         max(gCloud.maxSteps, 1u) * 2u, ign);

    gCloudOutput[dtid.xy] = float4(cloud.luminance, cloud.transmittance);
}
