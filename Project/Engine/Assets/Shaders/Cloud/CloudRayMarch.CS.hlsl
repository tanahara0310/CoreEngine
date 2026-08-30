/// @file CloudRayMarch.CS.hlsl
/// @brief 雲の半解像度レイマーチと時間再投影
/// @details 各ピクセル 1 レイ。球殻交差でマーチ区間を求め、SceneDepth で不透明遮蔽をクランプし、
///          MarchClouds へ渡す。出力は前乗算アルファ形式（rgb=前乗算輝度, a=透過率）。
///          サンプル位相をフレームごとに回し、前フレームの結果と混ぜることで
///          1 フレームあたりの反復数を増やさずに実効サンプル数を稼ぐ。

#include "Common/CloudCirrus.hlsli"

Texture2D<float> gSceneDepth : register(t3);
Texture2D<float4> gCloudHistory : register(t6);
Texture3D<float4> gCameraVolumeLUT : register(t7);
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
    float opaqueDist = kCloudNoOpaqueDistance;
    if (ndcDepth < kCloudDepthFarThreshold)
    {
        float4 wp = mul(float4(ndc, ndcDepth, 1.0f), gCloud.invViewProj);
        wp /= wp.w;
        opaqueDist = length(wp.xyz - rayOrigin);
        marchEnd = min(marchEnd, opaqueDist);
    }

    // 画面空間 IGN でサンプル位相をジッタし、等距離サンプル面が作る
    // 放射状のバンディングをノイズへ分散させる。再投影が有効なときだけ
    // フレームで位相を回す（無効なら毎フレーム同じ模様＝静止画ではちらつかない）
    bool reproject = gCloud.reprojectEnabled > 0.5f;
    uint jitterFrame = reproject ? gCloud.frameIndex : 0u;
    float ign = InterleavedGradientNoise(float2(dtid.xy), jitterFrame);

    CloudMarchResult cloud = MarchClouds(rayOrigin, rayDir, marchStart, marchEnd,
                                         max(gCloud.maxSteps, 1u) * 2u, ign);

    ApplyCloudAerialPerspective(cloud, uv, gCameraVolumeLUT, gLUTSampler);

    // 巻雲は積雲層より高いので常に奥。空気遠近は層ごとの距離で別々に掛ける
    CloudMarchResult cirrus = SampleCirrusShell(rayOrigin, rayDir, opaqueDist);
    ApplyCloudAerialPerspective(cirrus, uv, gCameraVolumeLUT, gLUTSampler);
    cloud = CompositeCloudLayers(cloud, cirrus);

    float4 current = float4(cloud.luminance, cloud.transmittance);

    // ===== 時間再投影 =====
    // 雲の代表距離のワールド点を前フレームのビュー射影へ投げて履歴 UV を得る。
    // 雲は風で動くが、10km 先の雲が 8m/s で流れても 1 フレームの画面移動は
    // 半解像度 1 画素に遠く届かないので、カメラ運動だけの再投影で足りる。
    if (reproject && cloud.distance > 0.0f)
    {
        float3 worldPos = rayOrigin + rayDir * cloud.distance;
        float4 prevClip = mul(float4(worldPos, 1.0f), gCloud.prevViewProj);
        if (prevClip.w > 0.0f)
        {
            float2 prevNdc = prevClip.xy / prevClip.w;
            float2 prevUv = float2(prevNdc.x * 0.5f + 0.5f, 0.5f - prevNdc.y * 0.5f);
            if (all(prevUv >= 0.0f) && all(prevUv <= 1.0f))
            {
                float4 history = gCloudHistory.SampleLevel(gLUTSampler, prevUv, 0);

                // 履歴と現フレームの透過率が食い違う画素は、雲の縁が動いた・
                // 別の雲を掴んだということなので現フレーム寄りへ倒す。
                // 固定の混合率だけだと動きの速い縁がゴーストになる
                float disagree = saturate(abs(history.a - current.a) / gCloud.reprojectTolerance);
                float alpha = lerp(gCloud.reprojectBlendMin, 1.0f, disagree);
                current = lerp(history, current, alpha);
            }
        }
    }

    gCloudOutput[dtid.xy] = current;
}
