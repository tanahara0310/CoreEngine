// ============================================================
// RT シャドウ 解決パス（トレース解像度 → フル解像度）
//
// 【役割】
//   トレース〜デノイズはハーフ解像度で回っている。最終シャドウマスクは
//   DeferredLighting がフル解像度のピクセル座標で Load するため、
//   ここでフル解像度へ引き伸ばす。
//
// 【なぜバイラテラルか】
//   単純なバイリニアだと物体の輪郭を跨いで影が滲む（前景の影が背景に漏れる）。
//   トレース解像度の各テクセルは「あるフル解像度ピクセル」を代表しているので、
//   その代表ピクセルの深度・法線と、いま解決したいピクセルの深度・法線を
//   比べて重み付けする。
//
// 【フル解像度モード】
//   gTraceScale == 1 のときは 1:1 の写しになる。
//
// 入力レイアウトは RTShadowTemporal.CS.hlsl と共通（.x = シャドウ値）。
// ============================================================

#include "../Include/Common/DepthReconstruction.hlsli"

Texture2D<float4> gTraceShadow : register(t0);     // トレース解像度のシャドウ
Texture2D<float> gSceneDepth : register(t1);       // フル解像度 深度
Texture2D<float4> gNormalRoughness : register(t2); // フル解像度 法線

RWTexture2D<float> gOutputShadow : register(u0);   // フル解像度 最終マスク

cbuffer ResolveConstants : register(b0)
{
    int gFullWidth;
    int gFullHeight;
    int gTraceWidth;
    int gTraceHeight;
    float gProjM33;
    float gProjM43;
    int gTraceScale;
    int gTraceOffsetX;
    int gTraceOffsetY;
    float gPhiDepth;
    int gPad0_;
    int gPad1_;
};

/// @brief トレース座標 → そのテクセルが代表するフル解像度ピクセル座標
/// @details 2x2 の固定代表点を使う。巡回オフセット（gTraceOffset）を使うと
///          バイラテラル重みが毎フレーム変わり、輪郭近くのピクセルが採用サンプルの
///          切り替わりで静止シーンでもちらつく。テンポラル蓄積後の値は 2x2 の
///          平均を代表しているので、ガイドも固定の中央代表点で取るのが整合する。
int2 TraceToFull(int2 traceCoord)
{
    int2 full = traceCoord * gTraceScale + (gTraceScale >> 1);
    return min(full, int2(gFullWidth - 1, gFullHeight - 1));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    int2 coord = int2(dispatchID.xy);
    if (coord.x >= gFullWidth || coord.y >= gFullHeight)
        return;

    // 等倍のときは素通し
    if (gTraceScale == 1)
    {
        gOutputShadow[coord] = gTraceShadow.Load(int3(coord, 0)).x;
        return;
    }

    const float2 projZW = float2(gProjM33, gProjM43);

    float centerNdcDepth = gSceneDepth.Load(int3(coord, 0));
    if (IsBackgroundDepth(centerNdcDepth))
    {
        // 背景は影なし
        gOutputShadow[coord] = 1.0f;
        return;
    }

    float centerDepth = LinearizeViewDepth(centerNdcDepth, projZW);
    float3 centerNormal = normalize(gNormalRoughness.Load(int3(coord, 0)).rgb * 2.0f - 1.0f);

    // ===== このフル解像度ピクセルを囲む 2x2 のトレース座標 =====
    //   トレース座標 t の代表点は t*scale + scale/2 なので、
    //   フル座標 coord に対応する連続トレース座標は (coord - scale/2) / scale。
    //   旧実装は coord/gTraceScale を左上として +0/+1 を取っていたため、
    //   2x2 の窓が常に右下へ半テクセルずれ、偶数ピクセルと奇数ピクセルで
    //   重み配分が大きく食い違っていた（＝影の縁に 2px 周期の市松模様が出る）。
    float2 continuousTrace = (float2(coord) - float(gTraceScale >> 1)) / float(gTraceScale);
    int2 baseTrace = int2(floor(continuousTrace));
    float2 subPixel = continuousTrace - float2(baseTrace);

    float shadowSum = 0.0f;
    float weightSum = 0.0f;
    float fallback = 0.0f;
    float fallbackWeight = 0.0f;

    [unroll]
    for (int dy = 0; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = 0; dx <= 1; ++dx)
        {
            int2 traceCoord = clamp(baseTrace + int2(dx, dy),
                                    int2(0, 0),
                                    int2(gTraceWidth - 1, gTraceHeight - 1));
            int2 sourceFull = TraceToFull(traceCoord);

            float s = gTraceShadow.Load(int3(traceCoord, 0)).x;

            // バイリニア重み
            float wSpatial = ((dx == 0) ? (1.0f - subPixel.x) : subPixel.x)
                           * ((dy == 0) ? (1.0f - subPixel.y) : subPixel.y);

            // どのサンプルも幾何的に一致しなかったときのための素朴な平均
            fallback += s * wSpatial;
            fallbackWeight += wSpatial;

            float sNdcDepth = gSceneDepth.Load(int3(sourceFull, 0));
            if (IsBackgroundDepth(sNdcDepth))
                continue;

            float sDepth = LinearizeViewDepth(sNdcDepth, projZW);
            float3 sNormal = normalize(gNormalRoughness.Load(int3(sourceFull, 0)).rgb * 2.0f - 1.0f);

            // 相対深度差で見る（遠景ほど絶対差が大きくなるため）
            float relativeDepthDiff = abs(centerDepth - sDepth) / max(centerDepth, 1e-3f);
            float wDepth = exp(-relativeDepthDiff * gPhiDepth);
            float wNormal = pow(max(0.0f, dot(centerNormal, sNormal)), 16.0f);

            float w = wSpatial * wDepth * wNormal;
            shadowSum += s * w;
            weightSum += w;
        }
    }

    // 幾何的に一致するサンプルが無い（輪郭の細い部分など）ときは
    // 空間重みだけの平均へフォールバックする。ここで 1.0（影なし）に
    // 逃がすと輪郭に白い縁が出るので必ず実測値を使う。
    float result = (weightSum > 1e-4f)
        ? (shadowSum / weightSum)
        : ((fallbackWeight > 1e-4f) ? (fallback / fallbackWeight) : 1.0f);

    gOutputShadow[coord] = result;
}
