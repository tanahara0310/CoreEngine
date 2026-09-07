/// @file CloudVoxelMarch.CS.hlsl
/// @brief ブロック雲の半解像度 DDA ボクセルトラバーサル
/// @details CloudRayMarch.CS と同じ入出力契約（半解像度・前乗算アルファ・SceneDepth 遮蔽）を
///          持つ差し替えパス。マーチだけが固定ステップから DDA へ変わる。
///          出力形式が同じなので、合成（CloudComposite.CS）とゴッドレイは無改造で通る。
/// @note ブロック雲では巻雲シェルと時間再投影を切っている（VolumetricCloudManager が
///       定数バッファへ 0 を送る）ので、履歴の読み込みも巻雲の合成も行わない。
///       ジッタも要らない。DDA はセル境界を厳密に取るため、サンプル位相の概念が無い。

#include "Common/CloudVoxelMarch.hlsli"

Texture2D<float> gSceneDepth : register(t3);
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
    uint depthW, depthH;
    gSceneDepth.GetDimensions(depthW, depthH);
    int2 fullPix = clamp(int2(uv * float2(depthW, depthH)), int2(0, 0), int2(depthW - 1, depthH - 1));
    float ndcDepth = gSceneDepth.Load(int3(fullPix, 0));
    if (ndcDepth < kCloudDepthFarThreshold)
    {
        float4 wp = mul(float4(ndc, ndcDepth, 1.0f), gCloud.invViewProj);
        wp /= wp.w;
        marchEnd = min(marchEnd, length(wp.xyz - rayOrigin));
    }

    // 予算はセル数。DDA の 1 反復はサンライトマーチを伴わないぶん体積マーチより桁違いに
    // 軽いので、予算を厚めに取って MaxMarchDistance 側が常に効くようにする
    // （予算が先に尽きると、その距離でフェードが掛かって遠方の雲が消える）
    CloudMarchResult cloud = MarchCloudVoxels(rayOrigin, rayDir, marchStart, marchEnd,
                                              max(gCloud.maxSteps, 1u) * kCloudVoxelBudgetScale);

    ApplyCloudAerialPerspective(cloud, uv, gCameraVolumeLUT, gLUTSampler);

    gCloudOutput[dtid.xy] = float4(cloud.luminance, cloud.transmittance);
}
