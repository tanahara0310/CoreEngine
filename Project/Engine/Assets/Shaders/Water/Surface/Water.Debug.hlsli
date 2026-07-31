// ============================================================
// 水面デバッグ可視化（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// 本体の合成ロジック（main）から診断コードを切り離すためのファイル。
// 表示モードの番号は C++ 側 WaterDebugViewMode と
// WaterSurfaceDebugPanel.cpp の kWaterDebugViewNames に対応させること。
//
// 【include 位置の契約】
// このファイルは Water.PS.hlsl の main() 直前で include すること。
// 以下のグローバルが宣言済みであることを前提にしている（HLSL ではテクスチャや
// サンプラを引数で持ち回れないため、ここだけは暗黙依存を許容している）:
//   cbuffer : gDepthDebugViewMode / gDepthFadeDebugScale / gReflectionEnabled /
//             gFresnelBaseReflectance / gSkyEnvReflectionEnabled
//   資源    : gSceneColor / gSkyEnvironmentMap / gReflectionTexture /
//             gRTWaterRefractionColor / gLinearClamp
//   関数    : VisualizeDepthValue / VisualizeJacobian / VisualizeRTRefractionReason /
//             ResolveWaterTransmissionColor / SampleRTWaterRefraction /
//             IsRTColorValid / FresnelSchlick / SampleGlossyReflection
//   定数    : kWaterReflectionMicroRoughness
//
// 計算済みの値はすべて WaterDebugContext で明示的に受け取る（暗黙参照しない）。
// ============================================================
#ifndef WATER_DEBUG_INCLUDED
#define WATER_DEBUG_INCLUDED

/// @brief デバッグ表示が参照する、本体の合成過程で求まった値一式
struct WaterDebugContext
{
    float2 screenUV;
    uint2 pixelCoord;

    // 深度・水柱厚さ
    float sceneDepthNDC;
    float waterDepthNDC;
    float sceneDepthView;
    float waterDepthView;
    float waterColumn;
    bool hasValidDepthFade;

    // 合成の各段
    float3 transmittance;
    float3 transmissionColor;
    float3 reflectColor;
    float reflectanceWeight;
    float3 finalWaterComposite;
    float surfaceCoverage;

    // 幾何
    float3 geomNormal;
    float3 viewDir;
    float cosTheta;
    float4 jacobianData;
};

/// @brief 現在のデバッグ表示モードに対応する色を返す
/// @return 表示色（アルファは呼び出し側で 1.0 にする）
float3 ResolveWaterDebugColor(WaterDebugContext ctx)
{
    // 1: 生の深度（R=背景 NDC / G=水面 NDC / B=差分、赤=背景が水面より手前）
    if (gDepthDebugViewMode == 1)
    {
        float rawDelta = saturate((ctx.sceneDepthNDC - ctx.waterDepthNDC) * max(gDepthFadeDebugScale, 1.0f));
        if (ctx.sceneDepthNDC <= ctx.waterDepthNDC + 1.0e-5f)
        {
            return float3(1.0f, 0.0f, 0.0f);
        }
        return float3(ctx.sceneDepthNDC, ctx.waterDepthNDC, rawDelta);
    }

    // 2: 線形深度（R=背景 / G=水面 / B=差分）
    if (gDepthDebugViewMode == 2)
    {
        float sceneLinear = saturate(ctx.sceneDepthView / max(gDepthFadeDebugScale, 1.0e-4f));
        float waterLinear = saturate(ctx.waterDepthView / max(gDepthFadeDebugScale, 1.0e-4f));
        return float3(
            sceneLinear,
            waterLinear,
            saturate((ctx.sceneDepthView - ctx.waterDepthView) / max(gDepthFadeDebugScale, 1.0e-4f)));
    }

    // 3: 水柱厚さ（指数マッピング）
    if (gDepthDebugViewMode == 3)
    {
        float debugValue = 1.0f - exp(-ctx.waterColumn * max(gDepthFadeDebugScale, 1.0e-4f));
        return VisualizeDepthValue(debugValue);
    }

    // 4: スクリーン UV
    if (gDepthDebugViewMode == 4)
    {
        return float3(ctx.screenUV, 0.0f);
    }

    // 5: シーンカラー（水面越しの背景として実際に使う色）
    if (gDepthDebugViewMode == 5)
    {
        return ResolveWaterTransmissionColor(ctx.pixelCoord, ctx.screenUV);
    }

    // 6: 実際に合成へ使われる反射色（グロッシー化・雲合成まで済んだもの）。
    //    生のテクスチャを出すと雲の上書き合成が原因のまだらが映らず切り分けを誤る。
    if (gDepthDebugViewMode == 6)
    {
        return gReflectionEnabled ? ctx.reflectColor : float3(1.0f, 0.0f, 1.0f);
    }

    // 7: 面法線（平坦化前）で見たフレネル。合成に使う値はモード 13。
    if (gDepthDebugViewMode == 7)
    {
        float debugCosTheta = saturate(dot(ctx.geomNormal, ctx.viewDir));
        float debugFresnel = FresnelSchlick(debugCosTheta, saturate(gFresnelBaseReflectance));
        return VisualizeDepthValue(debugFresnel);
    }

    // 8: RT 屈折の生の色
    if (gDepthDebugViewMode == 8)
    {
        return SampleRTWaterRefraction(ctx.pixelCoord).rgb;
    }

    // 9: RT 屈折の失敗理由コード
    if (gDepthDebugViewMode == 9)
    {
        return VisualizeRTRefractionReason(SampleRTWaterRefraction(ctx.pixelCoord).a);
    }

    // 10: RT 屈折とスクリーン空間シーン色の差分（成功時）／失敗理由（失敗時）
    if (gDepthDebugViewMode == 10)
    {
        float4 rtRefraction = SampleRTWaterRefraction(ctx.pixelCoord);
        float3 sceneColor = gSceneColor.Sample(gLinearClamp, ctx.screenUV).rgb;
        float rtSuccess = IsRTColorValid(rtRefraction.a);
        return rtSuccess > 0.5f
            ? abs(rtRefraction.rgb - sceneColor) * 4.0f
            : VisualizeRTRefractionReason(rtRefraction.a);
    }

    // 11: 透過光（吸収・散乱適用後）
    if (gDepthDebugViewMode == 11)
    {
        return ctx.transmissionColor;
    }

    // 12: 波長別透過率（無効時はマゼンタ）
    if (gDepthDebugViewMode == 12)
    {
        return ctx.hasValidDepthFade ? saturate(ctx.transmittance) : float3(1.0f, 0.0f, 1.0f);
    }

    // 13: 実際の反射率（フレネル × 幾何遮蔽 × スケール）
    if (gDepthDebugViewMode == 13)
    {
        return VisualizeDepthValue(ctx.reflectanceWeight);
    }

    // 14: 最終合成
    if (gDepthDebugViewMode == 14)
    {
        return ctx.finalWaterComposite;
    }

    // 15: RT 屈折の色取得成否マスク（緑=成功 / 赤=失敗）
    if (gDepthDebugViewMode == 15)
    {
        float rtSuccess = IsRTColorValid(SampleRTWaterRefraction(ctx.pixelCoord).a);
        return lerp(float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), rtSuccess);
    }

    // 16: FFT ヤコビアン（砕波候補・圧縮・折り返し）
    if (gDepthDebugViewMode == 16)
    {
        return VisualizeJacobian(ctx.jacobianData);
    }

    // ---- 17〜19: 反射色の構成要素を単独表示（まだらの切り分け用）----
    // 17: 生の反射テクスチャ（グロッシー化・雲合成なし）。ここに出るなら反射源が原因。
    if (gDepthDebugViewMode == 17)
    {
        return gReflectionEnabled
            ? gReflectionTexture.Sample(gLinearClamp, ctx.screenUV).rgb
            : float3(1.0f, 0.0f, 1.0f);
    }

    // 18: 雲キューブマップの色。ここに出るならキューブマップの内容が原因。
    if (gDepthDebugViewMode == 18)
    {
        if (gSkyEnvReflectionEnabled == 0)
        {
            return float3(1.0f, 0.0f, 1.0f);
        }
        float3 dbgReflectDir = reflect(-ctx.viewDir, float3(0.0f, 1.0f, 0.0f));
        return gSkyEnvironmentMap.SampleLevel(gLinearClamp, dbgReflectDir, 0.0f).rgb;
    }

    // 19: 雲上書きの実効強度（R=雲の被覆 / G=輝度比減衰後の実効上書き量）
    if (gDepthDebugViewMode == 19)
    {
        if (gSkyEnvReflectionEnabled == 0)
        {
            return float3(1.0f, 0.0f, 1.0f);
        }
        float3 dbgReflectDir = reflect(-ctx.viewDir, float3(0.0f, 1.0f, 0.0f));
        const float kDbgEnvMipCount = 5.0f;
        const float kDbgEnvMip = kWaterReflectionMicroRoughness * (kDbgEnvMipCount - 1.0f);
        float4 dbgSkyEnv = gSkyEnvironmentMap.SampleLevel(gLinearClamp, dbgReflectDir, kDbgEnvMip);
        float dbgCloudOpacity = saturate(1.0f - dbgSkyEnv.a);

        float dbgSkyLuma = Luminance(SampleGlossyReflection(ctx.screenUV, 1.0f - ctx.cosTheta));
        float dbgCloudLuma = Luminance(dbgSkyEnv.rgb);
        float dbgDarkRatio = saturate(dbgCloudLuma / max(dbgSkyLuma, 1.0e-5f));
        float dbgOverlayScale = lerp(0.25f, 1.0f, dbgDarkRatio);
        float dbgHorizonFade = smoothstep(0.08f, 0.30f, dbgReflectDir.y);

        return float3(dbgCloudOpacity, dbgCloudOpacity * dbgOverlayScale * dbgHorizonFade, 0.0f);
    }

    // ---- 20〜22: フレネル混合の端点を強制表示（まだらの犯人特定用）----
    // 20/21 のどちらに斑が出るかで、透過側と反射側のどちらが原因かを一意に確定する。
    // 20: reflectanceWeight = 0（純透過）の最終合成
    if (gDepthDebugViewMode == 20)
    {
        return lerp(ctx.transmissionColor, ctx.transmissionColor, ctx.surfaceCoverage);
    }

    // 21: reflectanceWeight = 1（純反射）の最終合成
    if (gDepthDebugViewMode == 21)
    {
        return lerp(ctx.transmissionColor, ctx.reflectColor, ctx.surfaceCoverage);
    }

    // 22: 反射と透過の輝度差。明るいほど、そこでフレネルが振れると斑が見える。
    if (gDepthDebugViewMode == 22)
    {
        return abs(ctx.reflectColor - ctx.transmissionColor) * 3.0f;
    }

    // 未知のモード（黄色）
    return float3(1.0f, 1.0f, 0.0f);
}

#endif // WATER_DEBUG_INCLUDED
