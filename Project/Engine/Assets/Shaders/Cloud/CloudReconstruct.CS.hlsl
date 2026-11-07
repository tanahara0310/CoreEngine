/// @file CloudReconstruct.CS.hlsl
/// @brief 間引きレイマーチの結果と前フレームの雲バッファから、今フレームの半解像度の雲バッファを組み立てる
/// @details 今フレームにレイを撃った画素は、その結果を前フレームの雲バッファと混ぜる。
///          撃っていない画素は前フレームの雲バッファを再投影して引き継ぐ。
///          画面外から入ってきた画素と、不透明物の前後関係が変わった画素は、
///          近くのセルで今フレーム撃った結果のうち奥行きの揃うものから補間する。
///          色は前乗算アルファ（rgb=前乗算輝度, a=透過率）、距離は x=雲の代表距離, y=マーチの終端距離。

#include "Common/CloudCommon.hlsli"

ConstantBuffer<CloudConstants> gCloud : register(b0);
Texture2D<float> gSceneDepth : register(t0);
Texture2D<float4> gTraceColor : register(t1);
Texture2D<float2> gTraceDepth : register(t2);
Texture2D<float4> gCloudHistory : register(t3);
Texture2D<float2> gCloudDepthHistory : register(t4);
SamplerState gLinearClamp : register(s0);
RWTexture2D<float4> gCloudOutput : register(u0);
RWTexture2D<float2> gCloudDepthOutput : register(u1);

/// @brief ワールド点を前フレームの画面 UV へ投げる
/// @return 前フレームの画面内に入るとき true
bool ReprojectToPrevUv(float3 worldPos, out float2 prevUv)
{
    prevUv = float2(0.0f, 0.0f);
    float4 prevClip = mul(float4(worldPos, 1.0f), gCloud.prevViewProj);
    if (prevClip.w <= 0.0f)
    {
        return false;
    }
    float2 prevNdc = prevClip.xy / prevClip.w;
    prevUv = float2(prevNdc.x * 0.5f + 0.5f, 0.5f - prevNdc.y * 0.5f);
    return all(prevUv >= 0.0f) && all(prevUv <= 1.0f);
}

/// @brief 2 つのマーチ終端距離がどれだけ同じ奥行きとみなせるか（1 = 一致）
/// @details 距離の絶対差は手前ほど厳しく効かせたいので相対差で見る。許容量 0 は常に一致
float MarchEndAgreement(float a, float b)
{
    float tolerance = gCloud.upsampleDepthTolerance;
    if (tolerance <= 0.0f)
    {
        return 1.0f;
    }
    float relative = abs(a - b) / max(min(a, b), 1.0f);
    float ratio = relative / tolerance;
    return rcp(1.0f + ratio * ratio);
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint w = gCloud.outputWidth;
    uint h = gCloud.outputHeight;
    if (dtid.x >= w || dtid.y >= h)
    {
        return;
    }

    uint n = max(gCloud.traceInterleave, 1u);
    uint2 cell = dtid.xy / n;
    float4 traced = gTraceColor[cell];
    float2 tracedDepth = gTraceDepth[cell];

    float2 uv = (float2(dtid.xy) + 0.5f) / float2(w, h);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
    float4 farH = mul(float4(ndc, 1.0f, 1.0f), gCloud.invViewProj);
    farH /= farH.w;
    float3 rayOrigin = gCloud.cameraWorldPos;
    float3 rayDir = normalize(farH.xyz - rayOrigin);

    bool reproject = gCloud.reprojectEnabled > 0.5f;

    // ===== 今フレームにレイを撃った画素 =====
    // 雲の代表距離のワールド点を前フレームのビュー射影へ投げて履歴 UV を得る。
    // 雲は風で動くが、10km 先の雲が 8m/s で流れても 1 フレームの画面移動は
    // 半解像度 1 画素に遠く届かないので、カメラ運動だけの再投影で足りる。
    if (all(dtid.xy == CloudTracedPixel(cell, gCloud)))
    {
        float4 current = traced;
        float2 prevUv;
        if (reproject && tracedDepth.x > 0.0f
            && ReprojectToPrevUv(rayOrigin + rayDir * tracedDepth.x, prevUv))
        {
            float4 history = gCloudHistory.SampleLevel(gLinearClamp, prevUv, 0);

            // 履歴と現フレームの透過率が食い違う画素は、雲の縁が動いた・
            // 別の雲を掴んだということなので現フレーム寄りへ倒す。
            // 固定の混合率だけだと動きの速い縁がゴーストになる
            float disagree = saturate(abs(history.a - current.a) / gCloud.reprojectTolerance);
            float alpha = lerp(gCloud.reprojectBlendMin, 1.0f, disagree);
            current = lerp(history, current, alpha);
        }
        gCloudOutput[dtid.xy] = current;
        gCloudDepthOutput[dtid.xy] = tracedDepth;
        return;
    }

    // ===== 撃っていない画素: この画素のマーチ終端距離 =====
    // 前フレームの値がいまの画素と同じ奥行きまでマーチしたものかを、終端距離で見分ける
    float2 interval = CloudLayerInterval(rayOrigin, rayDir, gCloud);
    float marchEnd = min(interval.y, gCloud.maxMarchDistanceM);
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

    // ===== 前フレームの雲バッファを再投影して引き継ぐ =====
    // 再投影の距離は同じセルで撃った雲の距離を使う。そこに雲が無いときは雲層の入口で代える
    if (reproject)
    {
        float reprojectDist = (tracedDepth.x > 0.0f)
            ? tracedDepth.x
            : max(interval.x, gCloud.layerBottomAltitudeM);
        float2 prevUv;
        if (ReprojectToPrevUv(rayOrigin + rayDir * reprojectDist, prevUv))
        {
            int2 historyPix = clamp(int2(prevUv * float2(w, h)), int2(0, 0), int2(w - 1u, h - 1u));
            float2 historyDepth = gCloudDepthHistory[historyPix];
            if (MarchEndAgreement(historyDepth.y, marchEnd) >= 0.5f)
            {
                gCloudOutput[dtid.xy] = gCloudHistory.SampleLevel(gLinearClamp, prevUv, 0);
                gCloudDepthOutput[dtid.xy] = float2(historyDepth.x, marchEnd);
                return;
            }
        }
    }

    // ===== 前フレームの値が使えない画素: 近くのセルの結果から補間する =====
    // 今フレーム撃った画素はどのセルでもセル内の同じ位置にあるので、
    // その位置を基準にしたセル座標でバイリニア重みを取り、終端距離の揃わないセルを落とす
    float2 tracedOrigin = float2(CloudTracedPixel(uint2(0u, 0u), gCloud));
    float2 cellCoord = (float2(dtid.xy) - tracedOrigin) / float(n);
    float2 baseCoord = floor(cellCoord);
    float2 subTexel = cellCoord - baseCoord;
    int2 cellMax = int2((w + n - 1u) / n - 1u, (h + n - 1u) / n - 1u);

    float4 sum = 0.0f;
    float weightSum = 0.0f;
    float4 nearest = traced;
    float2 nearestDepth = tracedDepth;
    float nearestAgreement = -1.0f;

    [unroll] for (int i = 0; i < 4; ++i)
    {
        float2 corner = float2(i & 1, (i >> 1) & 1);
        int2 tapCell = clamp(int2(baseCoord + corner), int2(0, 0), cellMax);
        float2 axisWeight = lerp(1.0f - subTexel, subTexel, corner);

        float4 tapColor = gTraceColor[tapCell];
        float2 tapDepth = gTraceDepth[tapCell];
        float agreement = MarchEndAgreement(tapDepth.y, marchEnd);
        if (agreement > nearestAgreement)
        {
            nearestAgreement = agreement;
            nearest = tapColor;
            nearestDepth = tapDepth;
        }

        float weight = axisWeight.x * axisWeight.y * agreement;
        sum += tapColor * weight;
        weightSum += weight;
    }

    // 4 セルとも奥行きが食い違うとき（1 画素幅の稜線など）は最も近いセルをそのまま使う
    gCloudOutput[dtid.xy] = (weightSum > kCloudUpsampleMinWeight) ? (sum / weightSum) : nearest;
    gCloudDepthOutput[dtid.xy] = float2(nearestDepth.x, marchEnd);
}
