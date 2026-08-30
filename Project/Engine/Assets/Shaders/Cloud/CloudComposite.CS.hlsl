/// @file CloudComposite.CS.hlsl
/// @brief 半解像度の雲バッファをフル解像度 SceneColor へ深度考慮で合成する
/// @details 式: 出力 = 雲色（前乗算） + シーン色 × 透過率。
///          雲バッファはレイマーチ時に深度遮蔽済みなので、ここでは合成のみ行う。
///          gOutput は SceneColor 自身。各スレッドが自分のテクセルだけを読んで書き戻す。

#include "Common/CloudCommon.hlsli"

ConstantBuffer<CloudConstants> gCloud : register(b0);
Texture2D<float4> gCloudBuffer : register(t0);
Texture2D<float>  gSceneDepth : register(t1);
RWTexture2D<float4> gOutput : register(u0);

/// @brief 深度テクセルからカメラ〜不透明物の距離を求める
/// @return 不透明物が無い（遠クリップ）ときは kCloudNoOpaqueDistance
float OpaqueDistanceAt(int2 pix, float2 depthDims)
{
    float ndcDepth = gSceneDepth[pix];
    if (ndcDepth >= kCloudDepthFarThreshold)
    {
        return kCloudNoOpaqueDistance;
    }

    float2 pixUv = (float2(pix) + 0.5f) / depthDims;
    float2 ndc = float2(pixUv.x * 2.0f - 1.0f, (1.0f - pixUv.y) * 2.0f - 1.0f);
    float4 wp = mul(float4(ndc, ndcDepth, 1.0f), gCloud.invViewProj);
    return length(wp.xyz / wp.w - gCloud.cameraWorldPos);
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    gOutput.GetDimensions(width, height);
    if (dtid.x >= width || dtid.y >= height)
    {
        return;
    }

    uint depthW;
    uint depthH;
    gSceneDepth.GetDimensions(depthW, depthH);
    float2 depthDims = float2(depthW, depthH);
    int2 depthMax = int2(depthW - 1u, depthH - 1u);

    float2 uv = (float2(dtid.xy) + 0.5f) / float2(width, height);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
    float4 farH = mul(float4(ndc, 1.0f, 1.0f), gCloud.invViewProj);
    float3 rayDir = normalize(farH.xyz / farH.w - gCloud.cameraWorldPos);
    float2 interval = CloudLayerInterval(gCloud.cameraWorldPos, rayDir, gCloud);

    int2 centerPix = clamp(int2(uv * depthDims), int2(0, 0), depthMax);
    float centerDist = OpaqueDistanceAt(centerPix, depthDims);

    // ===== 積雲層が丸ごと不透明物の背後なら合成しない =====
    // 巻雲シェルは積雲層に交差しない方向にも出るので、
    // 「層に入らない」ことを理由に落としてはいけない（落とすと巻雲ごと消える）
    bool hasLayer = interval.y > interval.x;
    if (hasLayer && centerDist <= interval.x)
    {
        return; // SceneColor をそのまま残す
    }

    // 深度の比較はマーチ終端より先を見ても意味がないので、そこで頭打ちにする。
    // 頭打ちにしないと、遠方の海面と空（不透明物なし）が
    // 「どちらも雲層より先で終わる＝同じ雲」なのに別物として棄却される。
    float compareLimit = hasLayer ? min(interval.y, gCloud.maxMarchDistanceM)
                                  : gCloud.maxMarchDistanceM;
    centerDist = min(centerDist, compareLimit);

    // ===== 深度考慮アップサンプル =====
    // 半解像度テクセルは自分の中心画素の深度でマーチ終端を切っているので、
    // 中心画素と終端距離が食い違うタップの雲は別の奥行きのものになっている。
    // バイリニア重みへ深度の一致度を掛けることで、稜線をまたいで
    // 「空を貫いた雲」と「山で切られた雲」が混ざるのを防ぐ。
    // 深度が揃う大半の画素では重みがバイリニアそのものになり、階段は出ない。
    float2 cloudDims = float2(gCloud.outputWidth, gCloud.outputHeight);
    int2 cloudMax = int2(gCloud.outputWidth - 1u, gCloud.outputHeight - 1u);
    float2 cloudCoord = uv * cloudDims - 0.5f;
    float2 baseCoord = floor(cloudCoord);
    float2 subTexel = cloudCoord - baseCoord;

    float tolerance = gCloud.upsampleDepthTolerance;

    float4 sum = 0.0f;
    float weightSum = 0.0f;
    float4 nearest = 0.0f;
    float nearestWeight = -1.0f;

    [unroll] for (int i = 0; i < 4; ++i)
    {
        float2 corner = float2(i & 1, (i >> 1) & 1);
        int2 tapCoord = clamp(int2(baseCoord + corner), int2(0, 0), cloudMax);
        float2 axisWeight = lerp(1.0f - subTexel, subTexel, corner);
        float weight = axisWeight.x * axisWeight.y;

        float4 tap = gCloudBuffer[tapCoord];

        if (tolerance > 0.0f)
        {
            int2 tapPix = clamp(int2((float2(tapCoord) + 0.5f) / cloudDims * depthDims),
                                int2(0, 0), depthMax);
            float tapDist = min(OpaqueDistanceAt(tapPix, depthDims), compareLimit);

            // 距離の絶対差は手前ほど厳しく効かせたいので相対差で見る
            float relative = abs(tapDist - centerDist) / max(min(tapDist, centerDist), 1.0f);
            float ratio = relative / tolerance;
            float depthWeight = rcp(1.0f + ratio * ratio);
            weight *= depthWeight;

            if (depthWeight > nearestWeight)
            {
                nearestWeight = depthWeight;
                nearest = tap;
            }
        }

        sum += tap * weight;
        weightSum += weight;
    }

    // 4 タップとも深度が食い違うとき（1 画素幅の稜線など）は
    // 最も深度の近いタップをそのまま使う
    float4 cloud = (weightSum > kCloudUpsampleMinWeight) ? (sum / weightSum) : nearest;

    // cloud.a = 透過率。前乗算輝度 cloud.rgb をシーンの上に重ねる。
    float4 scene = gOutput[dtid.xy];
    gOutput[dtid.xy] = float4(cloud.rgb + scene.rgb * cloud.a, scene.a);
}
