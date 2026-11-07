/// @file CloudRayMarch.CS.hlsl
/// @brief 雲の間引きレイマーチ
/// @details 半解像度の画面を traceInterleave 四方のセルに分け、各セルで 1 画素だけレイを撃つ。
///          球殻交差でマーチ区間を求め、SceneDepth で不透明遮蔽をクランプし、MarchClouds へ渡す。
///          出力はセル単位で、色は前乗算アルファ（rgb=前乗算輝度, a=透過率）、
///          距離は x=雲の代表距離（雲なし = -1）, y=マーチの終端距離。
///          半解像度の雲バッファへの組み立てと時間再投影は CloudReconstruct.CS が行う。

#include "Common/CloudCirrus.hlsli"

Texture2D<float> gSceneDepth : register(t3);
Texture3D<float4> gCameraVolumeLUT : register(t7);
RWTexture2D<float4> gTraceColor : register(u0);
RWTexture2D<float2> gTraceDepth : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint w = gCloud.outputWidth;
    uint h = gCloud.outputHeight;
    uint n = max(gCloud.traceInterleave, 1u);
    if (dtid.x >= (w + n - 1u) / n || dtid.y >= (h + n - 1u) / n)
    {
        return;
    }

    // ===== レイ生成（セルの中で今フレーム撃つ半解像度ピクセル → NDC → ワールド方向） =====
    uint2 pixel = CloudTracedPixel(dtid.xy, gCloud);
    float2 uv = (float2(pixel) + 0.5f) / float2(w, h);
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
    // フレームで位相を回す（無効なら毎フレーム同じ模様＝静止画ではちらつかない）。
    // 1 つの画素は n×n フレームに 1 回撃つので、位相は n×n フレームごとに 1 つ進める。
    // ブロック雲は密度が二値なので、画素ごとに位相をずらしても縞がノイズへ分散せず、
    // 面の縁が 1 ステップ分ばらけて汚れるだけになる。全画素を同位相にして
    // ワールド固定のサンプル面を共有させ、縁を揃える
    bool reproject = gCloud.reprojectEnabled > 0.5f;
    uint jitterFrame = reproject ? gCloud.frameIndex / (n * n) : 0u;
    float ign = (gCloud.styleIndex == CLOUD_STYLE_REALISTIC)
        ? InterleavedGradientNoise(float2(pixel), jitterFrame)
        : 0.0f;

    CloudMarchResult cloud = MarchClouds(rayOrigin, rayDir, marchStart, marchEnd,
                                         max(gCloud.maxSteps, 1u) * 2u, ign);

    ApplyCloudAerialPerspective(cloud, uv, gCameraVolumeLUT, gLUTSampler);

    // 巻雲は積雲層より高いので常に奥。空気遠近は層ごとの距離で別々に掛ける
    CloudMarchResult cirrus = SampleCirrusShell(rayOrigin, rayDir, opaqueDist);
    ApplyCloudAerialPerspective(cirrus, uv, gCameraVolumeLUT, gLUTSampler);
    cloud = CompositeCloudLayers(cloud, cirrus);

    gTraceColor[dtid.xy] = float4(cloud.luminance, cloud.transmittance);
    gTraceDepth[dtid.xy] = float2(cloud.distance, marchEnd);
}
