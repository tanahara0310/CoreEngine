// ============================================================
// 水面のピクセルシェーダー
// 工程ごとの hlsli を順に呼んで合成する:
//   法線（WaterNormals）→ 水柱（WaterColumn）→ フレネル → 泡（WaterFoamAppearance）→
//   透過（WaterVolume）→ 反射（WaterReflection）→ 合成 → きらめき・波頭の透過（WaterSubsurface）→
//   空気遠近感・高さフォグ
// ============================================================
#include "WaterSurfaceResources.hlsli"
#include "WaterNormals.hlsli"
#include "WaterColumn.hlsli"
#include "WaterVolume.hlsli"
#include "WaterFoamAppearance.hlsli"
#include "WaterReflection.hlsli"
#include "WaterSubsurface.hlsli"
#include "Water.Debug.hlsli"

/// @brief 水面専用フォワードパス処理（ForwardMain の discard 処理を削除したバージョン）
/// 水面は常に描画され、alpha 値で透明度を制御する
VertexShaderOutput ToVertexShaderOutput(WaterPSInput input)
{
    VertexShaderOutput output;
    output.position = input.position;
    output.texcoord = input.texcoord;
    output.normal = input.normal;
    output.worldPosition = input.worldPosition;
    output.lightSpacePos = input.lightSpacePos;
    output.tangent = input.tangent;
    output.bitangent = input.bitangent;
    output.clipPosCurrent = input.clipPosCurrent;
    output.clipPosPrev = input.clipPosPrev;
    return output;
}

/// @brief 水面専用フォワード PBR 出力
/// @param surfaceNormal main() で 1 度だけ解決した水面法線（ResolveSurfaceNormal の結果）
PixelShaderOutput WaterForwardMain(WaterPSInput input, float3 surfaceNormal)
{
    VertexShaderOutput forwardInput = ToVertexShaderOutput(input);
    PixelShaderOutput output;

    forwardInput.normal = surfaceNormal;

    // 視線方向
    float3 toEye = normalize(gCamera.worldPosition - forwardInput.worldPosition);

    // アンリット
    if (gMaterial.enableLighting == 0)
    {
        output.color = float4(gMaterial.color.rgb, 1.0f);
        return output;
    }

    // 水面はテクスチャを使わず、マテリアル値のみで PBR パラメータを構成する
    float metallic = gMaterial.metallic;
    float roughness = max(gMaterial.roughness, 0.01f);
    float ao = 1.0f; // 水面は AO マップを持たないため遮蔽なし固定

    float3 albedo = gMaterial.color.rgb;

    // PBR ライティング
    output.color.rgb = CalculateAllLighting(forwardInput, albedo, metallic, roughness, ao, toEye, float4(1.0f, 1.0f, 1.0f, 1.0f));
    output.color.a = 1.0f;

    return output;
}

// ===== 水面の出力（SceneColor ＋ モーションベクター）=====
// モーションベクターは GBuffer の MotionVector（rg=NDC 差分, b=水面フラグ）へ書く。
struct WaterPixelOutput
{
    float4 color : SV_TARGET0;
    float4 motionVector : SV_TARGET1; ///< rg=NDC 差分（GBuffer.PS と同一規約）, b=1（水面フラグ）
};

/// @brief NDC 空間のモーションベクターを求める（GBuffer.PS.hlsl と同じ式）
/// @details TAA 側でジッタ差分を引く前提なので、ここではジッタ込みのクリップ座標から
///          そのまま差を取る（GBuffer と規約を揃えること）。
float2 ComputeWaterMotionVector(WaterPSInput input)
{
    const float2 ndcCurrent = input.clipPosCurrent.xy / max(input.clipPosCurrent.w, 1.0e-6f);
    const float2 ndcPrevious = input.clipPosPrev.xy / max(input.clipPosPrev.w, 1.0e-6f);
    return ndcCurrent - ndcPrevious;
}

WaterPixelOutput main(WaterPSInput input)
{
    // 岸の細かい格子の範囲の中は岸の格子が描くので、粗い格子の画素は描かない
    if (gShoreMeshEnabled != 0 && input.shoreMeshVertex < 0.5f
        && IsInsideShoreMesh(input.baseWorldXZ, gSeabedOriginXZ, 1.0f / gSeabedInvSize))
    {
        discard;
    }

    WaterPixelOutput waterOutput;
    waterOutput.motionVector = float4(ComputeWaterMotionVector(input), 1.0f, 0.0f);

    // ---- 1. 水面法線を 1 度だけ解決し、以降の工程で使い回す ----
    float unresolvedMeanSquareSlope;
    const float3 surfaceNormal = ResolveSurfaceNormal(input, unresolvedMeanSquareSlope);
    // 反射のにじみに使う、法線に入っていない傾き（カスケードより細かい波を含む）
    const float reflectionMeanSquareSlope = unresolvedMeanSquareSlope
        + ((gUseFFTOceanNormalMap != 0) ? gFineWaveMeanSquareSlope : 0.0f);

    // ---- 2. スクリーン UV ----
    uint sceneDepthWidth = 1;
    uint sceneDepthHeight = 1;
    gSceneDepth.GetDimensions(sceneDepthWidth, sceneDepthHeight);
    float2 screenUV = input.position.xy / float2(sceneDepthWidth, sceneDepthHeight);
    screenUV = saturate(screenUV);
    uint2 pixelCoord = min(uint2(input.position.xy), uint2(sceneDepthWidth - 1, sceneDepthHeight - 1));

    // ---- 3. メインライトの日向率（水より上の遮蔽物の影が水面に落ちているか）----
    // 太陽の鏡面反射（きらめき）には日向率をそのまま掛ける。
    // 拡散の項（泡・水中の散乱・波頭の透過）は、影の中でも直接光を kShadowLightFloor だけ残す。
    const float sunVisibility = (gSunVisibilityEnabled != 0)
        ? gWaterSunVisibility.Load(int3(pixelCoord, 0))
        : 1.0f;
    const float sunDiffuseVisibility = ApplyShadowLightFloor(sunVisibility);

    // ---- 4. 水面専用 PBR フォワード出力（discard なし）----
    // 空環境マップが有効なフレームでは、反射色は RT 反射か空キューブマップの色になり、
    // フォワード PBR の rgb は読まれないので計算を省く。
    const bool forwardColorUnused = gSkyEnvReflectionEnabled != 0;
    PixelShaderOutput output;
    if (forwardColorUnused)
    {
        output.color = float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    else
    {
        output = WaterForwardMain(input, surfaceNormal);
    }

    // ---- 5. 水柱と波長別の透過率 ----
    // σ が RGB で異なるため、同じ光路長でも赤→緑→青の順に減衰し、
    // 浅瀬エメラルド→深瀬青の色相遷移が指数則から自動的に生じる。
    float3 sigmaA = max(gAbsorptionCoeff, 0.0f);
    float3 sigmaS = max(gScatteringCoeff, 0.0f);
    float3 sigmaT = max(sigmaA + sigmaS, 1.0e-4f);

    const WaterColumnResult waterColumnResult =
        ResolveWaterColumn(input, surfaceNormal, screenUV, pixelCoord);
    const float waterColumn = waterColumnResult.column;
    const float waterDepthNDC = saturate(input.position.z);

    // Beer-Lambert 則（波長別）: exp(-σt·d)
    float3 transmittance = exp(-sigmaT * waterColumn);

    // ---- 6. 視線方向と Fresnel 係数 ----
    float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition);
    float3 geomNormal = surfaceNormal;

    // フレネルは反射像・透過像と同じ面法線で評価し、法線に入っていない傾きは見えている面で平均する
    float cosTheta = saturate(dot(surfaceNormal, viewDir));
    float fresnel = ComputeVisibleFacetFresnel(
        cosTheta, 0.5f * reflectionMeanSquareSlope, saturate(gFresnelBaseReflectance));
    float reflectanceWeight = saturate(fresnel * gFresnelReflectanceScale);

    // ---- 7. 泡（水面の上のレースと、水面下の白濁）----
    const WaterFoamResult foam = ResolveWaterFoam(
        input.baseWorldXZ, input.worldPosition, waterColumnResult.viewRayVerticalDepth,
        surfaceNormal, sunDiffuseVisibility);
    reflectanceWeight *= (1.0f - foam.surfaceFoam);

    // ---- 8. 透過（水面越しに見える水中の色）----
    float3 transmissionColor = ResolveWaterTransmission(
        pixelCoord, screenUV, transmittance, sigmaS, sigmaT, sunDiffuseVisibility);
    // 水面下の気泡は、その奥から透過してくる光を遮り、自分が散乱した光に置き換える
    transmissionColor = lerp(transmissionColor, foam.color * kFoamHazeTint, foam.bubbleHaze);
    // 水中の放射輝度は、水面から空気へ出るときに屈折で立体角が広がって 1/n² になる
    transmissionColor *= WaterRadianceExitScale();

    // ---- 9. 反射 ----
    // 反射が有効なとき、環境反射は反射像で置き換える（フォワード PBR の色へ加算しない）
    const float3 reflectColor = ResolveWaterReflection(
        screenUV, cosTheta, viewDir, geomNormal, reflectionMeanSquareSlope, output.color.rgb);

    // ---- 10. 合成 ----
    // 透過像（吸収・散乱を通した色）と反射像をフレネルで合成する
    float3 finalWaterComposite = lerp(transmissionColor, reflectColor, reflectanceWeight);

    // 水面の上の泡（レース）を重ねる（白ベタ禁止: gFoamOpacity < 1 で水面下の情報を残す）
    finalWaterComposite = lerp(finalWaterComposite, foam.color, foam.surfaceFoam);

    if (gDepthFadeDebugEnabled != 0)
    {
        WaterDebugContext debugContext;
        debugContext.screenUV = screenUV;
        debugContext.pixelCoord = pixelCoord;
        debugContext.sceneDepthNDC = waterColumnResult.sceneDepthNDC;
        debugContext.waterDepthNDC = waterDepthNDC;
        debugContext.sceneDepthView = waterColumnResult.sceneDepthView;
        debugContext.waterDepthView = waterColumnResult.waterDepthView;
        debugContext.waterColumn = waterColumn;
        debugContext.hasValidDepthFade = waterColumnResult.hasValidDepth;
        debugContext.transmittance = transmittance;
        debugContext.transmissionColor = transmissionColor;
        debugContext.reflectColor = reflectColor;
        debugContext.reflectanceWeight = reflectanceWeight;
        debugContext.finalWaterComposite = finalWaterComposite;
        debugContext.geomNormal = geomNormal;
        debugContext.viewDir = viewDir;
        debugContext.cosTheta = cosTheta;
        // ヤコビアン可視化は FFT テクスチャを引くので参照格子座標を渡す
        debugContext.worldXZ = input.baseWorldXZ;
        debugContext.foamMask = foam.mask;

        waterOutput.color = float4(ResolveWaterDebugColor(debugContext), 1.0f);
        return waterOutput;
    }

    output.color.rgb = finalWaterComposite;

    // ---- 11. 太陽のきらめき（ComputeSunGlintSpecular）----
    // 反射色が映り込みの像でないときはフォワード PBR の色（太陽のスペキュラ入り）が反射色なので、加算しない。
    // 泡は拡散層で鏡面のきらめきを持たない: 被覆分の輝度抑制＋ラフネス引き上げの両方。
    if (UsesReflectionImage())
    {
        output.color.rgb +=
            ComputeSunGlintSpecular(
                geomNormal, viewDir, foam.surfaceFoam, sunVisibility, unresolvedMeanSquareSlope)
            * (1.0f - foam.surfaceFoam);
    }

    // ---- 12. 波峰のサブサーフェス透過（逆光で波の背が緑に光る）----
    // 波を透過して視点へ出てくる光なので、水面で反射されずに「抜けてきた」分だけ、
    // つまり (1 - フレネル反射率) と、水から空気へ出る 1/n² を掛けて加算する。
    output.color.rgb += ComputeWaterSubsurfaceScattering(
        surfaceNormal, viewDir, input.waveHeight, sigmaS, sigmaT, foam.surfaceFoam, sunDiffuseVisibility)
        * (1.0f - reflectanceWeight) * WaterRadianceExitScale();

    // ---- 13. 空気遠近感（Aerial Perspective）----
    // 不透明パスへの合成（AerialPerspective.CS）は水面より前に終わっているため、
    // 水面自身の距離で同じ霞をここで適用する。屈折成分（背景）には背景自身の距離の
    // 霞が既に乗っているが、水面までの区間が重複適用されるのは半透明の慣例的な近似として許容する
    if (gAerialPerspectiveEnabled != 0)
    {
        output.color.rgb = ApplyAerialPerspective(
            output.color.rgb, input.worldPosition, screenUV, gLinearClamp);
    }

    // ---- 14. 高さフォグ ----
    // 水面は全画面合成パス（HeightFog.CS）より後に描かれるため、そこでは掛かっていない。
    // 不透明と同じ数式（Fog.hlsli）を水面自身の距離で適用する。
    // gFog は BaseModelRenderer がブレンドモードに応じて差す（水面は通常ブレンド＝内散乱あり）。
    output.color.rgb = ApplyFog(gFog, input.worldPosition, output.color.rgb);

    output.color.a = 1.0f;

    waterOutput.color = output.color;
    return waterOutput;
}
