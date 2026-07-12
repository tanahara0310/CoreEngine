/// @file GodRayMarch.CS.hlsl
/// @brief ゴッドレイ（雲の隙間の光芒）の半解像度レイマーチ
/// @details ビューレイに沿って単一散乱の内散乱を積分し、雲シャドウマップによる
///          「遮蔽あり − 遮蔽なし」の差分（≤0）を出力する。既存の Sky-View /
///          Aerial Perspective が加算済みの遮蔽なし内散乱と二重加算にならない合成モデル。
///          雲影の空気柱が暗くなり、切れ間の空気柱が明るく残ることで光芒の対比が生まれる。
///          加えて演出用の加算ミー項（mieBoost）を同じループで積分する。
///          出力は差分輝度（負値を含む float4）。

#include "../Atmosphere/Common/AtmosphereCommon.hlsli"
#include "Common/CloudCommon.hlsli"
#include "Common/GodRayCommon.hlsli"

ConstantBuffer<GodRayConstants> gGodRay : register(b0);
ConstantBuffer<AtmosphereConstants> gAtmosphere : register(b1);
Texture2D<float>  gCloudShadowMap : register(t0);
Texture2D<float4> gTransmittanceLUT : register(t1);
Texture2D<float>  gSceneDepth : register(t2);
Texture2D<float4> gCloudBuffer : register(t3); // 雲レイマーチ結果（a=雲透過率。同じ半解像度）
SamplerState gLUTSampler : register(s0);
RWTexture2D<float4> gGodRayOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint w = gGodRay.outputWidth;
    uint h = gGodRay.outputHeight;
    if (dtid.x >= w || dtid.y >= h)
    {
        return;
    }

    // ===== レイ生成（半解像度ピクセル → NDC → ワールド方向。CloudRayMarch と同一） =====
    float2 uv = (float2(dtid.xy) + 0.5f) / float2(w, h);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);

    float4 farH = mul(float4(ndc, 1.0f, 1.0f), gGodRay.invViewProj);
    farH /= farH.w;
    float3 rayOrigin = gGodRay.cameraWorldPos;
    float3 rayDir = normalize(farH.xyz - rayOrigin);

    // v1 ガード: カメラが雲底面近く/より上では上面シャドウマップの前提が崩れるため無効
    if (rayOrigin.y > gGodRay.shadowAnchorWorldY - 500.0f)
    {
        gGodRayOutput[dtid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // ===== マーチ区間 =====
    float tMax = gGodRay.maxDistanceM;

    // 不透明ジオメトリで打ち切り（深度テクスチャは実寸を Load）
    uint depthW, depthH;
    gSceneDepth.GetDimensions(depthW, depthH);
    int2 fullPix = int2(uv * float2(depthW, depthH));
    fullPix = clamp(fullPix, int2(0, 0), int2(depthW - 1, depthH - 1));
    float ndcDepth = gSceneDepth.Load(int3(fullPix, 0));
    if (ndcDepth < 0.9999999f)
    {
        float4 wp = mul(float4(ndc, ndcDepth, 1.0f), gGodRay.invViewProj);
        wp /= wp.w;
        tMax = min(tMax, length(wp.xyz - rayOrigin));
    }

    // 上向きレイは雲底面で打ち切り（光芒は雲デッキ下の空気で起きる）
    if (rayDir.y > 1e-4f)
    {
        float tPlane = (gGodRay.shadowAnchorWorldY - rayOrigin.y) / rayDir.y;
        tMax = min(tMax, tPlane);
    }

    if (tMax <= 0.0f)
    {
        gGodRayOutput[dtid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // ===== 積分準備 =====
    float3 toSun = -gAtmosphere.sunDirection;
    float cosTheta = dot(rayDir, toSun);
    float phaseM = HenyeyGreensteinPhase(gAtmosphere.miePhaseG, cosTheta);

    // 静的 IGN でサンプル位相をジッタ（時間依存にしない＝エンジン規約）
    float ign = InterleavedGradientNoise(float2(dtid.xy));

    uint stepCount = max(gGodRay.stepCount, 1u);
    float dt = tMax / stepCount;
    float dtKm = dt * 0.001f;

    // ===== 積分 =====
    // 差分項はミー散乱のみで積分する。レイリー（青が支配的）を含めて減算すると
    // 白い雲・空から青だけが抜けて黄緑に転ぶ（実際の雲影下の空は多重散乱が
    // 青灰色を保つため、分光的な単一散乱の減算は色相を歪める）。
    // ミーは波長非依存（スカラー係数）なので減算は無彩色の暗化になり、
    // 前方散乱ピーク（太陽方向で強い）も光芒の見た目と一致する。
    // shaft: 雲遮蔽ありのミー内散乱 / full: 遮蔽なし
    float3 shaft = float3(0.0f, 0.0f, 0.0f);
    float3 full = float3(0.0f, 0.0f, 0.0f);
    float3 opticalDepth = float3(0.0f, 0.0f, 0.0f);

    [loop] for (uint i = 0; i < stepCount; ++i)
    {
        float t = (i + ign) * dt;
        float3 P = rayOrigin + rayDir * t;
        float hKm = max((P.y - gGodRay.groundLevelY) * 0.001f, 0.0f);

        // 視線透過率の消散はレイリー＋ミー＋オゾンの全成分で正しく積む
        opticalDepth += ComputeExtinction(hKm, gAtmosphere) * dtKm;
        float3 T = exp(-opticalDepth);

        float3 density = ComputeMediumDensity(hKm, gAtmosphere);
        float sigmaSMie = gAtmosphere.mieScattering * density.y; // [1/km]

        // 太陽までの透過率（アースシャドウ込み。日没後は自動的に 0 へ落ちる）
        float3 posAtmo = float3(0.0f, gAtmosphere.planetRadiusKm + hKm, 0.0f);
        float3 sunT = SampleTransmittanceToSun(gTransmittanceLUT, gLUTSampler,
                                               posAtmo, toSun, gAtmosphere);

        float shadow = SampleCloudShadow(gCloudShadowMap, gLUTSampler, P, toSun, gGodRay);

        float3 common = T * sunT * (sigmaSMie * phaseM * dtKm);
        shaft += common * shadow;
        full += common;
    }

    // 差分項（≤0）だけ雲の透過率でスケールする:
    // 雲に覆われたピクセルでは「遮蔽なしの空気の内散乱」が既に雲の合成
    // （scene×cloud.a）で消えており、そこからさらに引くと過剰減算（雲の変色）になる。
    // 一方、加算ブースト項は「カメラと雲の間の空気が太陽に照らされて輝く」成分で、
    // マーチ区間は雲底面で打ち切っている＝常に雲の手前の空気なので、
    // 雲ピクセルの上に加算するのが正しい（遠くの雲の手前に光の筋がかかる）。
    // （gCloudBuffer は本バッファと同じ半解像度なので Load で 1:1 対応する）
    float cloudTransmittance = gCloudBuffer.Load(int3(dtid.xy, 0)).a;
    float3 delta = (shaft - full) * (gGodRay.intensity * cloudTransmittance)
                 + shaft * gGodRay.mieBoost;
    delta *= gAtmosphere.sunColor * gAtmosphere.sunIntensity;

    gGodRayOutput[dtid.xy] = float4(delta, 1.0f);
}
