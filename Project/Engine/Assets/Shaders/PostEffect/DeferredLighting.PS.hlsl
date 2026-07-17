#include "FullScreen.hlsli"
#include "../Include/Lighting/LightStructures.hlsli"
#include "../Include/Shadow/ShadowCalculation.hlsli"
#include "../Include/PBR/PBR.hlsli"

// ============================================================
// G-Buffer テクスチャ
// ============================================================
Texture2D<float4> gAlbedoAO : register(t0);
Texture2D<float4> gNormalRoughness : register(t1);
Texture2D<float4> gEmissiveMetallic : register(t2);
Texture2D<float4> gWorldPosition : register(t3);

// ============================================================
// ライトデータ
// ============================================================
ConstantBuffer<LightCounts> gLightCounts : register(b1);
StructuredBuffer<DirectionalLightData> gDirectionalLights : register(t4);
StructuredBuffer<PointLightData> gPointLights : register(t5);
StructuredBuffer<SpotLightData> gSpotLights : register(t6);
StructuredBuffer<AreaLightData> gAreaLights : register(t7);

// ============================================================
// シャドウ
// ============================================================
Texture2D<float> gShadowMap : register(t8);
SamplerComparisonState gShadowSampler : register(s1);

struct LightVP
{
    float4x4 mat;
};
ConstantBuffer<LightVP> gLightViewProjection : register(b3);

// ============================================================
// カメラ
// ============================================================
struct Camera
{
    float3 worldPosition;
};
ConstantBuffer<Camera> gCamera : register(b2);

// ============================================================
// IBL テクスチャ
// ============================================================
TextureCube<float4> gIrradianceMap : register(t9);
TextureCube<float4> gPrefilteredMap : register(t10);
Texture2D<float2> gBRDFLUT : register(t11);

// ============================================================
// RT シャドウマスク（ライトごとに独立したテクスチャ）
// ============================================================
Texture2D<float> gRTShadowMask0 : register(t12);
Texture2D<float> gRTShadowMask1 : register(t13);
Texture2D<float> gRTShadowMask2 : register(t14);
Texture2D<float> gRTShadowMask3 : register(t15);

// ============================================================
// SSAO テクスチャ（スクリーンスペース AO）
// ============================================================
Texture2D<float4> gSSAO : register(t16);

// ============================================================
// Water Caustics テクスチャ
// ============================================================
Texture2D<float4> gWaterCaustics : register(t17);

struct WaterCausticsDebug
{
    uint debugViewMode;
    float debugDisplayScale;
    float2 padding;
};
ConstantBuffer<WaterCausticsDebug> gWaterCausticsDebug : register(b5);

// ============================================================
// IBL パラメータ（シーン共通）
// ============================================================
struct IBLParams
{
    float3 environmentRotation; // 環境マップ XYZ 回転（ラジアン）
    float iblIntensity; // IBL 強度 (0.0-∞, デフォルト 1.0)
};
ConstantBuffer<IBLParams> gIBLParams : register(b4);

// ============================================================
// 空アンビエント（大気散乱 Sky-View LUT の SH9 射影。Sky Light 相当）
// ============================================================
// gSkyIrradianceSH には放射照度畳み込み済みの SH9 係数が入っており、
// Σ c_i・Y_i(N) がそのまま「法線 N に対する空からの放射照度 / π」になる。
StructuredBuffer<float4> gSkyIrradianceSH : register(t18);

struct SkyAmbientParams
{
    uint enabled;          // 1 = 空アンビエント有効（大気アクティブなフレームのみ）
    float scale;           // 空の輝度単位 → サーフェス光単位の変換係数
    uint specularEnabled;  // 1 = 空スペキュラIBL（空＋雲キューブマップの環境反射）有効
    float padding;
};
ConstantBuffer<SkyAmbientParams> gSkyAmbient : register(b6);

// プリフィルタ済み空スペキュラキューブマップ（空＋雲。Phase 3b: Sky Light のスペキュラ相当）
// AtmosphereManager が Sky-View LUT＋雲レイマーチから毎フレーム生成する。
// 5 ミップ（mip = roughness × 4）。輝度は空（SkyAtmosphere.PS）と同一ドメイン。
TextureCube<float4> gSkySpecularMap : register(t19);
static const float kSkySpecularMipCount = 5.0f;

/// @brief 解析的 EnvBRDF 近似（Karis "Physically Based Shading on Mobile"）
/// @details Split-Sum の BRDF 積分項を LUT 無しで近似する。シーン IBL の gBRDFLUT は
///          IBLSystem がセットアップされたシーン専用資産のため、大気スペキュラでは使わない。
float2 EnvBRDFApprox(float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

/// @brief SH9 係数から法線方向の空の放射照度/π を評価する
float3 EvaluateSkyIrradiance(float3 n)
{
    float basis[9];
    basis[0] = 0.282095f;
    basis[1] = 0.488603f * n.y;
    basis[2] = 0.488603f * n.z;
    basis[3] = 0.488603f * n.x;
    basis[4] = 1.092548f * n.x * n.y;
    basis[5] = 1.092548f * n.y * n.z;
    basis[6] = 0.315392f * (3.0f * n.z * n.z - 1.0f);
    basis[7] = 1.092548f * n.x * n.z;
    basis[8] = 0.546274f * (n.x * n.x - n.y * n.y);

    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        irradiance += gSkyIrradianceSH[i].rgb * basis[i];
    }
    // SH の帯域打ち切りによる負のリンギングを防ぐ
    return max(irradiance, 0.0f);
}

SamplerState gSampler : register(s0);

// ============================================================
// ACES トーンマッピング
// ============================================================
float3 ACESFilm(float3 x)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ============================================================
// IBL（PBR.hlsli CalculateFullIBL と同じロジック）
// ============================================================
float3 CalculateDeferredIBL(float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0, float ao)
{
    float NdotV = max(dot(N, V), 0.0f);
    float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);

    // === Diffuse IBL ===
    float3 rotatedN = RotateVector(N, gIBLParams.environmentRotation);
    float3 irradiance = gIrradianceMap.SampleLevel(gSampler, rotatedN, 0.0f).rgb;
    float3 kD = (1.0f - F) * (1.0f - metallic);
    float3 diffuseIBL = kD * albedo * irradiance;

    // === Specular IBL ===
    float3 R = normalize(reflect(-V, N));
    float3 rotatedR = RotateVector(R, gIBLParams.environmentRotation);
    float mipLevel = roughness * float(MAX_PREFILTERED_MIP_LEVELS - 1);
    float3 prefilteredColor = gPrefilteredMap.SampleLevel(gSampler, rotatedR, mipLevel).rgb;
    float2 envBRDF = gBRDFLUT.Sample(gSampler, float2(NdotV, roughness));
    float3 specularIBL = prefilteredColor * (F * envBRDF.x + envBRDF.y);

    return (diffuseIBL + specularIBL) * ao * gIBLParams.iblIntensity;
}

// ============================================================
// 入出力
// ============================================================
struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};
struct PixelShaderOutput
{
    float4 color : SV_Target;
};

// ============================================================
// メインシェーダー
// ============================================================
PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;

    // G-Buffer は Load() でポイントサンプリング（バイリニア補間によるフラグ/法線の破損を防止）
    int3 loadCoord = int3(input.position.xy, 0);
    float4 albedoAO = gAlbedoAO.Load(loadCoord);
    float4 normalRoughness = gNormalRoughness.Load(loadCoord);
    float4 emissiveMetallic = gEmissiveMetallic.Load(loadCoord);
    float4 worldPosSample = gWorldPosition.Load(loadCoord);

    // ===== 背景ピクセル検出 =====
    // GBuffer は worldPosition.a >= 1.0 を書き込む。
    // クリア後の背景ピクセルは a = 0 なのでここで早期リターン。
    if (worldPosSample.a < 0.5f)
    {
        output.color = float4(0.1f, 0.25f, 0.5f, 1.0f);
        return output;
    }

    if (gWaterCausticsDebug.debugViewMode != 0)
    {
        float3 rawCaustics = 0.0f.xxx;
        float causticsW, causticsH;
        gWaterCaustics.GetDimensions(causticsW, causticsH);
        if (causticsW > 1.0f && causticsH > 1.0f)
        {
            float2 causticsUV = (input.position.xy + 0.5f.xx) / float2(causticsW, causticsH);
            rawCaustics = gWaterCaustics.SampleLevel(gSampler, causticsUV, 0.0f).rgb * gWaterCausticsDebug.debugDisplayScale;
        }

        if (gWaterCausticsDebug.debugViewMode == 1)
        {
            output.color = float4(rawCaustics, 1.0f);
            return output;
        }

        float luminance = dot(rawCaustics, float3(0.2126f, 0.7152f, 0.0722f));
        output.color = float4(luminance.xxx, 1.0f);
        return output;
    }

    // ===== マテリアルフラグのデコード =====
    // GBuffer.PS.hlsl が worldPosition.a にエンコードしたフラグを読み取る。
    // 2 = PBR（マテリアルの IBL オプトアウト）, 3 = PBR + IBL
    int pixelType = int(round(worldPosSample.a));

    // ===== アンリットマテリアル検出 =====
    // GBuffer.PS.hlsl は enableLighting==0 のとき normalRoughness.a=0 のセンチネル値を書き込み、
    // emissiveMetallic.rgb にアンリットカラーを格納する。
    if (normalRoughness.a <= 0.0f)
    {
        output.color = float4(emissiveMetallic.rgb, 1.0f);
        return output;
    }

    // 共通パラメータ展開
    float3 albedo = albedoAO.rgb;
    float3 N = normalize(normalRoughness.rgb * 2.0f - 1.0f);
    float3 worldPos = worldPosSample.xyz;
    float3 V = normalize(gCamera.worldPosition - worldPos);

    // ===== ライト空間座標（PCFシャドウフォールバック用） =====
    float4 lightSpacePos = mul(float4(worldPos, 1.0f), gLightViewProjection.mat);

    // ===== RT シャドウマスク配列（ライトごとに独立） =====
    // 幅1以下のテクスチャは未使用（未ディスパッチ）
    float rtW0, rtH0;
    gRTShadowMask0.GetDimensions(rtW0, rtH0);
    bool useRTShadow = (rtW0 > 1.0f && rtH0 > 1.0f);

    // ===== RT シャドウ デバッグ表示 =====
#if 0  // 1 でデバッグ表示ON、0 で通常描画
    if (useRTShadow)
    {
        // 赤(影=0) → 緑(光=1) のグラデーション
        // ソフトシャドウが効いていれば影の境界に黄色〜のグラデーションが表示される
        output.color = float4(1.0f - baseShadow, baseShadow, 0.0f, 1.0f);
    }
    else
    {
        // 青 = RT シャドウ未使用（PCF フォールバック）
        output.color = float4(0.0f, 0.0f, 1.0f, 1.0f);
    }
    return output;
#endif

    // ============================================================
    // PBR ライティングパス（常にここに到達）
    // ============================================================
    // pixelType: 2=PBR（IBLオプトアウト）, 3=PBR+IBL
    // IBL はシーンに Irradiance マップがバインドされている場合のみ有効（幅1以下=未設定）
    float iblW, iblH;
    gIrradianceMap.GetDimensions(iblW, iblH);
    const bool sceneHasIBL = (iblW > 1.0f);
    const bool enableIBL = (pixelType == 3) && sceneHasIBL;

    float ao = saturate(albedoAO.a);

    // SSAO テクスチャが有効な場合（幅 > 1）は AO 値に乗算する
    {
        float ssaoW, ssaoH;
        gSSAO.GetDimensions(ssaoW, ssaoH);
        if (ssaoW > 1.0f && ssaoH > 1.0f)
        {
            float ssaoVal = gSSAO.Load(loadCoord).r;
            ao *= ssaoVal;
        }
    }

    float roughness = saturate(normalRoughness.a);
    float metallic = saturate(emissiveMetallic.a);
    float3 emissive = emissiveMetallic.rgb;
    float3 F0 = lerp(float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0), albedo, metallic);

    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    // ============================================================
    // PBR ライティング
    // ============================================================

    // ディレクショナルライト
    for (uint di = 0; di < gLightCounts.directionalLightCount; ++di)
    {
        DirectionalLightData dL = gDirectionalLights[di];
        if (!dL.enabled)
            continue;
        float3 L = normalize(-dL.direction);

        // ライトインデックスに対応する RT シャドウマスクを選択
        float shadowFactor;
        if (useRTShadow)
        {
            float rtVal;
            if      (di == 0) rtVal = gRTShadowMask0.Load(loadCoord).r;
            else if (di == 1) rtVal = gRTShadowMask1.Load(loadCoord).r;
            else if (di == 2) rtVal = gRTShadowMask2.Load(loadCoord).r;
            else              rtVal = gRTShadowMask3.Load(loadCoord).r;
            shadowFactor = rtVal;
        }
        else
        {
            shadowFactor = CalculateShadow(lightSpacePos, N, dL.direction, gShadowMap, gShadowSampler);
        }
        shadowFactor = lerp(0.3f, 1.0f, shadowFactor);

        Lo += CalculatePBRLighting(N, V, L, dL.color.rgb, dL.intensity, albedo, metallic, roughness, ao)
              * shadowFactor;
    }

    // ポイントライト（PBR: 逆二乗則 + 距離レンジ減衰）
    for (uint pi = 0; pi < gLightCounts.pointLightCount; ++pi)
    {
        PointLightData pL = gPointLights[pi];
        if (!pL.enabled)
            continue;
        float3 tv = pL.position - worldPos;
        float d = length(tv);
        if (d >= pL.radius)
            continue;
        float3 L = normalize(tv);
        float attenuation = 1.0f / (1.0f + pL.decay * d * d);
        float distRatio = d / pL.radius;
        float rangeFactor = saturate(1.0f - distRatio * distRatio * distRatio * distRatio);
        attenuation *= rangeFactor * rangeFactor;
        Lo += CalculatePBRLighting(N, V, L, pL.color.rgb, pL.intensity * attenuation, albedo, metallic, roughness, ao);
    }

    // スポットライト（PBR: 逆二乗則 + コーン減衰）
    for (uint si = 0; si < gLightCounts.spotLightCount; ++si)
    {
        SpotLightData sL = gSpotLights[si];
        if (!sL.enabled)
            continue;
        float3 tv = sL.position - worldPos;
        float d = length(tv);
        if (d >= sL.distance)
            continue;
        float3 L = normalize(tv);
        float attenuation = 1.0f / (1.0f + sL.decay * d * d);
        float sDistRatio = d / sL.distance;
        float sRangeFactor = saturate(1.0f - sDistRatio * sDistRatio * sDistRatio * sDistRatio);
        attenuation *= sRangeFactor * sRangeFactor;
        float cosTheta = dot(-L, normalize(sL.direction));
        if (cosTheta < sL.cosAngle)
            continue;
        float spotFactor = saturate((cosTheta - sL.cosAngle) / (sL.cosFalloffStart - sL.cosAngle));
        attenuation *= spotFactor;
        Lo += CalculatePBRLighting(N, V, L, sL.color.rgb, sL.intensity * attenuation, albedo, metallic, roughness, ao);
    }

    // エリアライト（PBR: 最近接点計算 + PBR ライティング）
    for (uint ai = 0; ai < gLightCounts.areaLightCount; ++ai)
    {
        AreaLightData aL = gAreaLights[ai];
        if (!aL.enabled)
            continue;

        // 最近接点計算（Forward の Lighting.hlsli CalculateAreaLight と同等）
        float3 toLight = aL.position - worldPos;
        float distToPlane = dot(toLight, aL.normal);
        float3 projectedPoint = worldPos + aL.normal * distToPlane;
        float3 offset = projectedPoint - aL.position;
        float u = dot(offset, aL.right);
        float v = dot(offset, aL.up);
        float halfW = aL.width * 0.5f;
        float halfH = aL.height * 0.5f;
        float clampedU = clamp(u, -halfW, halfW);
        float clampedV = clamp(v, -halfH, halfH);
        float3 closestPoint = aL.position + aL.right * clampedU + aL.up * clampedV;

        float3 toClosest = closestPoint - worldPos;
        float dist = length(toClosest);
        if (dist >= aL.range)
            continue;
        float3 L = toClosest / max(dist, 0.001f);

        float distFactor = 1.0f - saturate(dist / aL.range);
        float distAttenuation = distFactor * distFactor;

        float shapeFactor = 1.0f;
        float outsideU = max(0.0f, abs(u) - halfW);
        float outsideV = max(0.0f, abs(v) - halfH);
        float outsideDist = sqrt(outsideU * outsideU + outsideV * outsideV);
        if (outsideDist > 0.001f)
        {
            float falloffRange = max(halfW, halfH);
            shapeFactor = 1.0f - saturate(outsideDist / falloffRange);
            shapeFactor = shapeFactor * shapeFactor * shapeFactor;
        }

        float facingFactor = max(0.0f, dot(aL.normal, -L));
        float finalAttenuation = distAttenuation * shapeFactor * facingFactor;

        Lo += CalculatePBRLighting(N, V, L, aL.color.rgb, aL.intensity * finalAttenuation, albedo, metallic, roughness, ao);
    }

    // IBL アンビエント
    // IBL 無効時（シーンにマップが無い / マテリアルがオプトアウト）は
    // ハーフランバートアンビエントにフォールバック
    float3 ambient = float3(0.0f, 0.0f, 0.0f);
    {
        // -------------------------------------------------------
        // アンビエントのシャドウ方針:
        //   RTシャドウ有効時: ボックスフィルタで平滑化したRTシャドウを使用する。
        //     → 直接光と同じ影境界を保ちつつノイズを抑制。
        //     → PCFとRTは影境界の位置・幅が異なるため、RT有効時にPCFを使うと
        //        ペナンブラ領域で両者がずれてエッジノイズが発生する。
        //        PCFが広いほどずれ幅が拡大し、ノイズが悪化する。
        //   RTシャドウ無効時: PCFシャドウ（5x5 平滑化済み）を使用する。
        // -------------------------------------------------------
        if (enableIBL)
        {
            // IBLアンビエントにもシャドウを適用する（近似）。
            // 物理的には間接光に影は落ちないが、適用しないと直接光のシャドウが
            // IBLアンビエントに打ち消されて影が見えなくなるため近似的に適用する。
            float iblShadow = 1.0f;
            if (gLightCounts.directionalLightCount > 0 && gDirectionalLights[0].enabled)
            {
                if (useRTShadow)
                    // テンポラル蓄積済みのRTシャドウは既に十分滑らかなので、追加ブラーは不要
                    iblShadow = gRTShadowMask0.Load(loadCoord).r;
                else
                    iblShadow = CalculateShadow(lightSpacePos, N, gDirectionalLights[0].direction, gShadowMap, gShadowSampler);
                iblShadow = lerp(0.3f, 1.0f, iblShadow);
            }
            ambient = CalculateDeferredIBL(N, V, albedo, metallic, roughness, F0, ao) * iblShadow;
        }
        else if (gSkyAmbient.enabled != 0)
        {
            // ===== 空アンビエント（大気散乱由来。Sky Light 相当） =====
            // 大気の Sky-View LUT を SH9 射影した放射照度で照らす。
            // 昼は青みがかった環境光・夕方はオレンジ・夜はほぼゼロと時刻に追従する。
            // 影は IBL 分岐と同様にメインライトのシャドウを弱く適用する
            float skyShadow = 1.0f;
            if (gLightCounts.directionalLightCount > 0 && gDirectionalLights[0].enabled)
            {
                if (useRTShadow)
                    skyShadow = gRTShadowMask0.Load(loadCoord).r;
                else
                    skyShadow = CalculateShadow(lightSpacePos, N, gDirectionalLights[0].direction, gShadowMap, gShadowSampler);
                skyShadow = lerp(0.3f, 1.0f, skyShadow);
            }

            // 拡散: SH9 放射照度。金属は拡散反射しないため減衰させる
            float3 skyIrradiance = EvaluateSkyIrradiance(N);
            ambient = albedo * (1.0f - metallic) * skyIrradiance * gSkyAmbient.scale * ao * skyShadow;

            // スペキュラ: プリフィルタ済み空キューブマップによる環境反射（Phase 3b）。
            // 空・雲・太陽まわりのグレアが roughness に応じたぼけで映り込む。
            // 拡散と同じ skyAmbientScale で空の輝度ドメインをサーフェス光単位へ変換する。
            if (gSkyAmbient.specularEnabled != 0)
            {
                float3 R = reflect(-V, N);
                float NdotV = saturate(dot(N, V));
                float mipLevel = roughness * (kSkySpecularMipCount - 1.0f);
                float3 prefiltered = gSkySpecularMap.SampleLevel(gSampler, R, mipLevel).rgb;
                float2 envBRDF = EnvBRDFApprox(roughness, NdotV);
                float3 specular = prefiltered * (F0 * envBRDF.x + envBRDF.y);
                ambient += specular * gSkyAmbient.scale * ao * skyShadow;
            }
        }
        else
        {
            for (uint hli = 0; hli < gLightCounts.directionalLightCount; ++hli)
            {
                DirectionalLightData hL = gDirectionalLights[hli];
                if (!hL.enabled)
                    continue;
                float3 L = normalize(-hL.direction);

                // アンビエントにもシャドウを適用する。
                // 適用しないとアンビエントが直接光のシャドウを打ち消して影が見えなくなる。
                float ambientShadow;
                if (useRTShadow)
                {
                    // テンポラル蓄積済みのRTシャドウは既に十分滑らかなので、追加ブラーは不要
                    if      (hli == 0) ambientShadow = gRTShadowMask0.Load(loadCoord).r;
                    else if (hli == 1) ambientShadow = gRTShadowMask1.Load(loadCoord).r;
                    else if (hli == 2) ambientShadow = gRTShadowMask2.Load(loadCoord).r;
                    else               ambientShadow = gRTShadowMask3.Load(loadCoord).r;
                }
                else
                {
                    ambientShadow = CalculateShadow(lightSpacePos, N, hL.direction, gShadowMap, gShadowSampler);
                }
                ambientShadow = lerp(0.3f, 1.0f, ambientShadow);

                ambient += CalculateHalfLambertAmbient(N, L, hL.color.rgb, hL.intensity, albedo, metallic, ao) * ambientShadow;
            }
        }
    }

    // HDR値をそのまま出力（トーンマッピングはポストエフェクトチェーンで適用）
    float3 waterCaustics = 0.0f.xxx;
    {
        float causticsW, causticsH;
        gWaterCaustics.GetDimensions(causticsW, causticsH);
        if (causticsW > 1.0f && causticsH > 1.0f)
        {
            float2 causticsUV = (input.position.xy + 0.5f.xx) / float2(causticsW, causticsH);
            waterCaustics = gWaterCaustics.SampleLevel(gSampler, causticsUV, 0.0f).rgb;
            float3 causticsAlbedoScale = lerp(0.35f.xxx, albedo, 0.65f);
            float causticsAOScale = lerp(0.45f, 1.0f, ao);
            float causticsMetallicScale = lerp(1.0f, 0.25f, metallic);
            static const float kWaterCausticsCompositeScale = 4.5f;
            waterCaustics *= causticsAlbedoScale * causticsAOScale * causticsMetallicScale * kWaterCausticsCompositeScale;
        }
    }

    float3 color = Lo + ambient + emissive + waterCaustics;
    output.color = float4(color, 1.0f);
    return output;
}
