#include "FullScreen.hlsli"
#include "../Common/LightStructures.hlsli"
#include "../Common/ShadowCalculation.hlsli"
#include "../Common/PBR.hlsli"

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
// RT シャドウマスク（DXR レイトレーシング結果、無効時は 1.0 でフォールバック）
// ============================================================
Texture2D<float> gRTShadowMask : register(t12);

// ============================================================
// IBL パラメータ（シーン共通）
// ============================================================
struct IBLParams
{
    float3 environmentRotation; // 環境マップ XYZ 回転（ラジアン）
    float iblIntensity; // IBL 強度 (0.0-∞, デフォルト 1.0)
};
ConstantBuffer<IBLParams> gIBLParams : register(b4);

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

    // ===== マテリアルフラグのデコード =====
    // GBuffer.PS.hlsl が worldPosition.a にエンコードしたフラグを読み取る。
    // 2 = PBR + IBL無効, 3 = PBR + IBL有効
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

    // ===== ライト空間座標（シャドウ計算用） =====
    float4 lightSpacePos = mul(float4(worldPos, 1.0f), gLightViewProjection.mat);

    // ===== シャドウファクターの計算 =====
    // RT シャドウマスクが有効な場合はそちらを使用、無効時は PCF シャドウにフォールバック
    float rtShadowWidth, rtShadowHeight;
    gRTShadowMask.GetDimensions(rtShadowWidth, rtShadowHeight);
    bool useRTShadow = (rtShadowWidth > 1.0f && rtShadowHeight > 1.0f);

    float baseShadow;
    if (useRTShadow)
    {
        // RTシャドウはシェーダー側でコーンサンプリングによるソフト化済みのため
        // 空間フィルタは不要。直接ロードして使用する。
        baseShadow = gRTShadowMask.Load(loadCoord).r;
    }
    else
    {
        baseShadow = CalculateShadow(lightSpacePos, N, float3(0, -1, 0), gShadowMap, gShadowSampler);
    }

    // ===== RT シャドウ デバッグ表示 =====
    // 赤=影(0), 緑=光(1), 青=RT未使用
    // ソフトシャドウが機能していれば影の境界にグラデーション（黄〜緑）が見える
    // 問題が解決したら #if 0 に戻すこと
#if 1  // 1 でデバッグ表示ON、0 で通常描画
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
    // pixelType: 2=PBR, 3=PBR+IBL, 4=Lambert, 5=HalfLambert
    const bool enableIBL = (pixelType == 3);
    const bool useLambert = (pixelType == 4);
    const bool useHalfLambert = (pixelType == 5);
    const bool useTraditional = useLambert || useHalfLambert;

    float ao = saturate(albedoAO.a);
    float roughness = saturate(normalRoughness.a);
    float metallic = saturate(emissiveMetallic.a);
    float3 emissive = emissiveMetallic.rgb;
    float3 F0 = lerp(float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0), albedo, metallic);

    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    if (useTraditional)
    {
        // ============================================================
        // 従来シェーディング（Lambert / HalfLambert）
        // ============================================================

        // ディレクショナルライト
        for (uint di = 0; di < gLightCounts.directionalLightCount; ++di)
        {
            DirectionalLightData dL = gDirectionalLights[di];
            if (!dL.enabled)
                continue;
            float3 L = normalize(-dL.direction);
            // RT シャドウはメインライト(index 0)のみ、他は PCF
            float shadowFactor = (di == 0)
                ? baseShadow
                : CalculateShadow(lightSpacePos, N, dL.direction, gShadowMap, gShadowSampler);

            float3 diff;
            if (useLambert)
            {
                // Lambert: shadowFactor を結果に乗算（従来通り）
                diff = CalculateLambertDiffuse(N, L, dL.color.rgb, dL.intensity, albedo, ao)
                       * lerp(0.3f, 1.0f, shadowFactor);
            }
            else
            {
                // HalfLambert + RTシャドウ の共存方法：ライティングを分離する。
                //
                // HalfLambert（NdotL*0.5+0.5）はアンビエント的な光の回り込みを表す近似であり、
                // 影の中でも最低 0.5 の明るさを保つ設計のため、物理的なシャドウと直接掛け合わせると
                // 投影影が消えてしまう（shadow=0 でも halfLambert≈0.5 で明るいため）。
                //
                // そのため:
                //   直接光（Direct）  = Lambert で計算 × shadowFactor  → 影に入る
                //   間接補完（Wrap）  = HalfLambert の wrap 項のみ     → 影に入らない（光の回り込み）
                // として加算する。これにより投影影がきちんと落ちつつ、暗部も wrap で潰れない。
                float NdotL = dot(N, L);
                float3 lightRadiance = dL.color.rgb * dL.intensity * albedo * ao;

                // 直接光: Lambert × shadow
                float directTerm = max(NdotL, 0.0f) * lerp(0.3f, 1.0f, shadowFactor);

                // 光の回り込み: HalfLambert の「上乗せ」部分のみ（wrap 量を定数で調整可）
                static const float kWrapStrength = 0.5f; // 0=回り込みなし, 1=通常HalfLambert相当
                float wrapTerm = saturate(NdotL * 0.5f + 0.5f) * kWrapStrength;

                diff = lightRadiance * (directTerm + wrapTerm);
            }
            Lo += diff;
        }

        // ポイントライト
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
            float atten = 1.0f / (1.0f + pL.decay * d * d);
            atten *= saturate(1.0f - d / pL.radius);
            float3 diff = useLambert
                ? CalculateLambertDiffuse(N, L, pL.color.rgb, pL.intensity * atten, albedo, ao)
                : CalculateHalfLambertDiffuse(N, L, pL.color.rgb, pL.intensity * atten, albedo, ao);
            Lo += diff;
        }

        // スポットライト
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
            float atten = 1.0f / (1.0f + sL.decay * d * d);
            atten *= saturate(1.0f - d / sL.distance);
            float cosTheta = dot(-L, normalize(sL.direction));
            if (cosTheta < sL.cosAngle)
                continue;
            atten *= saturate((cosTheta - sL.cosAngle) / (sL.cosFalloffStart - sL.cosAngle));
            float3 diff = useLambert
                ? CalculateLambertDiffuse(N, L, sL.color.rgb, sL.intensity * atten, albedo, ao)
                : CalculateHalfLambertDiffuse(N, L, sL.color.rgb, sL.intensity * atten, albedo, ao);
            Lo += diff;
        }

        // エリアライト
        for (uint ai = 0; ai < gLightCounts.areaLightCount; ++ai)
        {
            AreaLightData aL = gAreaLights[ai];
            if (!aL.enabled)
                continue;
            float3 toLight = aL.position - worldPos;
            float distToPlane = dot(toLight, aL.normal);
            float3 projectedPoint = worldPos + aL.normal * distToPlane;
            float3 offset = projectedPoint - aL.position;
            float u = dot(offset, aL.right);
            float v = dot(offset, aL.up);
            float halfW = aL.width * 0.5f;
            float halfH = aL.height * 0.5f;
            float3 closestPoint = aL.position + aL.right * clamp(u, -halfW, halfW) + aL.up * clamp(v, -halfH, halfH);
            float3 toClosest = closestPoint - worldPos;
            float dist = length(toClosest);
            if (dist >= aL.range)
                continue;
            float3 L = toClosest / max(dist, 0.001f);
            float distFactor = 1.0f - saturate(dist / aL.range);
            float distAtten = distFactor * distFactor;
            float outsideU = max(0.0f, abs(u) - halfW);
            float outsideV = max(0.0f, abs(v) - halfH);
            float outsideDist = sqrt(outsideU * outsideU + outsideV * outsideV);
            float shapeFactor = 1.0f;
            if (outsideDist > 0.001f)
            {
                float falloffRange = max(halfW, halfH);
                shapeFactor = 1.0f - saturate(outsideDist / falloffRange);
                shapeFactor = shapeFactor * shapeFactor * shapeFactor;
            }
            float facingFactor = max(0.0f, dot(aL.normal, -L));
            float finalAtten = distAtten * shapeFactor * facingFactor;
            float3 diff = useLambert
                ? CalculateLambertDiffuse(N, L, aL.color.rgb, aL.intensity * finalAtten, albedo, ao)
                : CalculateHalfLambertDiffuse(N, L, aL.color.rgb, aL.intensity * finalAtten, albedo, ao);
            Lo += diff;
        }
    }
    else
    {
        // ============================================================
        // PBR ライティング（pixelType 2 / 3）
        // ============================================================

        // ディレクショナルライト
        for (uint di = 0; di < gLightCounts.directionalLightCount; ++di)
        {
            DirectionalLightData dL = gDirectionalLights[di];
            if (!dL.enabled)
                continue;
            float3 L = normalize(-dL.direction);

            // RT シャドウはメインライト(index 0)のみ、他は PCF
            float shadowFactor = (di == 0)
                ? baseShadow
                : CalculateShadow(lightSpacePos, N, dL.direction, gShadowMap, gShadowSampler);
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
            float rangeFactor = saturate(1.0f - d / pL.radius);
            attenuation *= rangeFactor;
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
            float rangeFactor = saturate(1.0f - d / sL.distance);
            attenuation *= rangeFactor;
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
    }

    // IBL アンビエント
    // 従来シェーディング（Lambert/HalfLambert）はアンビエントなし
    // PBR+IBL 無効時はハーフランバートアンビエントにフォールバック
    float3 ambient = float3(0.0f, 0.0f, 0.0f);
    if (!useTraditional)
    {
        if (enableIBL)
        {
            ambient = CalculateDeferredIBL(N, V, albedo, metallic, roughness, F0, ao);
        }
        else
        {
            for (uint hli = 0; hli < gLightCounts.directionalLightCount; ++hli)
            {
                DirectionalLightData hL = gDirectionalLights[hli];
                if (!hL.enabled)
                    continue;
                float3 L = normalize(-hL.direction);
                ambient += CalculateHalfLambertAmbient(N, L, hL.color.rgb, hL.intensity, albedo, metallic, ao);
            }
        }
    }

    // HDR値をそのまま出力（トーンマッピングはポストエフェクトチェーンで適用）
    float3 color = Lo + ambient + emissive;
    output.color = float4(color, 1.0f);
    return output;
}
