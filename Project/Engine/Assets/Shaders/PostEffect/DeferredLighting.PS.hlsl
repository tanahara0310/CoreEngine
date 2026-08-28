#include "FullScreen.hlsli"
#include "../Include/Lighting/LightStructures.hlsli"
#include "../Include/PBR/PBR.hlsli"
#include "../Include/Common/DepthReconstruction.hlsli"
#include "../Include/Common/ColorSpace.hlsli" // Luminance / ACESFilm
// 水中ライティング置換の関数群（PI / Luminance に依存するため PBR / ColorSpace の後）
#include "../Include/Lighting/UnderwaterLighting.hlsli"
#include "../Cloud/Common/CloudShadowCommon.hlsli"

// ============================================================
// G-Buffer テクスチャ
// ============================================================
Texture2D<float4> gAlbedoAO : register(t0);
Texture2D<float4> gNormalRoughness : register(t1);
Texture2D<float4> gEmissiveMetallic : register(t2);
Texture2D<float> gSceneDepth : register(t3); // WorldPosition ターゲット廃止に伴い深度から復元する

// ============================================================
// ライトデータ
// ============================================================
ConstantBuffer<LightCounts> gLightCounts : register(b1);
StructuredBuffer<DirectionalLightData> gDirectionalLights : register(t4);
StructuredBuffer<PointLightData> gPointLights : register(t5);
StructuredBuffer<SpotLightData> gSpotLights : register(t6);
StructuredBuffer<AreaLightData> gAreaLights : register(t7);

// ============================================================
// カメラ
// ============================================================
struct Camera
{
    float3 worldPosition;
};
ConstantBuffer<Camera> gCamera : register(b2);

// ============================================================
// 深度復元用（WorldPosition ターゲット廃止に伴う追加）
// ============================================================
// gCamera（b2）は BaseModelRenderer の WVP 用 CBV を共有しており、フリッカー防止のため
// フレーム更新時に 1 回しか書かれない（Camera::TransferMatrix 参照）。
// そのため invViewProj は毎ビュー確実に更新される専用 CBV として別に持つ。
struct DepthReconstructionParams
{
    float4x4 invViewProj;
};
ConstantBuffer<DepthReconstructionParams> gDepthReconstruction : register(b7);

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

// コースティクスのデバッグ表示＋水中ライティング設定
// （C++ 側 DeferredLightingTechnique::WaterCausticsDebugSettings と一致させること）
struct WaterCausticsDebug
{
    uint debugViewMode;
    float debugDisplayScale;
    // ===== 水中ライティング（直接光の二重計上排除） =====
    // 1 = 水中ピクセルのメインライト直接光をコースティクス（完全な透過直接光）で置換し、
    //     アンビエントを Beer–Lambert で減衰させる。RT コースティクス合成フレームのみ 1。
    uint waterVolumeEnabled;
    float waterHeight;         // 水面の基準高さ（波なしの平面近似）
    float2 regionCenterXZ;     // 水面メッシュのワールドXZ範囲（RTWaterCaustics と同じ矩形）
    float2 regionHalfExtentXZ;
    float3 absorptionCoeff;    // 波長依存の吸収係数 σa [1/m]（水面描画と同値）
    float padding0;
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

// ============================================================
// 雲シャドウ（雲が地面へ落とす影）
// ============================================================
// CloudShadowMapPass が Deferred ライティングより前に生成する太陽方向の雲透過率。
// 雲が無効なフレームは差されないので、寸法 1x1 のダミーで無効化を検出する。
Texture2D<float> gCloudShadowMap : register(t20);
ConstantBuffer<CloudShadowConstants> gCloudShadow : register(b8);

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
    float ndcDepth = gSceneDepth.Load(loadCoord);

    // ===== 背景ピクセル検出 =====
    // 深度クリア値（1.0）のピクセルは未描画の背景。
    if (IsBackgroundDepth(ndcDepth))
    {
        output.color = float4(0.1f, 0.25f, 0.5f, 1.0f);
        return output;
    }

    // ===== ワールド座標復元 =====
    float depthW, depthH;
    gSceneDepth.GetDimensions(depthW, depthH);
    float2 screenUV = (input.position.xy + 0.5f.xx) / float2(depthW, depthH);
    float3 worldPos = ReconstructWorldPosition(ScreenUVToNDC(screenUV), ndcDepth, gDepthReconstruction.invViewProj);

    // モード 1/2 は「コースティクス入力そのもの」の表示なのでここで即返す。
    // 3 以降は水中ライティング置換が絡むため、GBuffer 展開後（関数末尾）で処理する。
    if (gWaterCausticsDebug.debugViewMode == 1 || gWaterCausticsDebug.debugViewMode == 2)
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

        float luminance = Luminance(rawCaustics);
        output.color = float4(luminance.xxx, 1.0f);
        return output;
    }

    // ===== アンリットマテリアル検出 =====
    // GBuffer.PS.hlsl は enableLighting==0 のとき normalRoughness.a=0.0 の厳密なセンチネル値を書き込み、
    // emissiveMetallic.rgb にアンリットカラーを格納する。IBLオプトアウト（負値）と区別するため
    // <= ではなく == で判定する（ライト時のroughnessは0.01未満に丸められないため0との衝突は無い）。
    if (normalRoughness.a == 0.0f)
    {
        output.color = float4(emissiveMetallic.rgb, 1.0f);
        return output;
    }

    // ===== IBLフラグのデコード =====
    // GBuffer.PS.hlsl は IBL 有効時 roughness を正、オプトアウト時は負で書き込む（符号ビット埋め込み）。
    const bool materialWantsIBL = normalRoughness.a > 0.0f;

    // 共通パラメータ展開
    float3 albedo = albedoAO.rgb;
    float3 N = normalize(normalRoughness.rgb * 2.0f - 1.0f);
    float3 V = normalize(gCamera.worldPosition - worldPos);

    // ===== RT シャドウマスク配列（ライトごとに独立） =====
    // 幅1以下のテクスチャは未使用（未ディスパッチ）。その場合は影なしで描画する
    // （従来型シャドウマップのPCFフォールバックは2026-07-25に全廃・DXR前提）。
    float rtW0, rtH0;
    gRTShadowMask0.GetDimensions(rtW0, rtH0);
    bool useRTShadow = (rtW0 > 1.0f && rtH0 > 1.0f);

    // ===== 雲シャドウ =====
    // 雲が無効なフレームは差されないので、同じく寸法で有効・無効を判定する
    float csW, csH;
    gCloudShadowMap.GetDimensions(csW, csH);
    bool useCloudShadow = (csW > 1.0f && csH > 1.0f && gCloudShadow.sceneStrength > 0.0f);

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
    // IBL はシーンに Irradiance マップがバインドされている場合のみ有効（幅1以下=未設定）
    float iblW, iblH;
    gIrradianceMap.GetDimensions(iblW, iblH);
    const bool sceneHasIBL = (iblW > 1.0f);
    const bool enableIBL = materialWantsIBL && sceneHasIBL;

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

    float roughness = saturate(abs(normalRoughness.a));
    float metallic = saturate(emissiveMetallic.a);
    float3 emissive = emissiveMetallic.rgb;
    float3 F0 = lerp(float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0), albedo, metallic);

    // ===== 水中判定（直接光の二重計上排除） =====
    // 水中の床が受け取る太陽光は「水面で屈折・吸収・集光された透過光」であり、
    // それは RT コースティクスが全量を計算している。ここで通常の直接光も加算すると
    // 同じ光を二重に計上することになるため、水中ピクセルではメインライトの直接光を
    // コースティクスへクロスフェードで置換する。
    // 判定・濡れ暗色化・合成の詳細は Include/Lighting/UnderwaterLighting.hlsli を参照。
    // gWaterCaustics のサンプルはここで 1 回だけ行い、以降（合成・デバッグ）で再利用する。
    float4 waterCausticsSample = 0.0f.xxxx;
    bool hasWaterCaustics = false;
    {
        float causticsW, causticsH;
        gWaterCaustics.GetDimensions(causticsW, causticsH);
        if (causticsW > 1.0f && causticsH > 1.0f)
        {
            hasWaterCaustics = true;
            float2 causticsUV = (input.position.xy + 0.5f.xx) / float2(causticsW, causticsH);
            waterCausticsSample = gWaterCaustics.SampleLevel(gSampler, causticsUV, 0.0f);
        }
    }

    const UnderwaterContext underwater = BuildUnderwaterContext(
        waterCausticsSample,
        gWaterCausticsDebug.waterVolumeEnabled,
        gWaterCausticsDebug.waterHeight,
        gWaterCausticsDebug.absorptionCoeff,
        worldPos);
    const float underwaterFactor = underwater.factor;
    const float3 underwaterAmbientT = underwater.ambientTransmittance;

    // 濡れ暗色化（★F0 計算の後・albedo を使うすべてのライティングの前★）
    if (gWaterCausticsDebug.waterVolumeEnabled != 0)
    {
        albedo = ApplyWetDarkening(albedo, underwaterFactor);
    }

    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    // 水中置換で「消したメインライト直接光」の量（デバッグ用の連続性チェックに使う）。
    // 波打ち際で waterCaustics とこの値が一致していれば水面線で明るさが跳ねない。
    float3 replacedMainLight = float3(0.0f, 0.0f, 0.0f);

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

        // ライトインデックスに対応する RT シャドウマスクを選択（未使用時は影なし）
        float shadowFactor = 1.0f;
        if (useRTShadow)
        {
            if      (di == 0) shadowFactor = gRTShadowMask0.Load(loadCoord).r;
            else if (di == 1) shadowFactor = gRTShadowMask1.Load(loadCoord).r;
            else if (di == 2) shadowFactor = gRTShadowMask2.Load(loadCoord).r;
            else              shadowFactor = gRTShadowMask3.Load(loadCoord).r;
        }
        shadowFactor = lerp(0.3f, 1.0f, shadowFactor);

        // 雲影。RT シャドウとは別に底上げする（雲影は柔らかく、影の中でも空からの
        // 光が回り込むので、ジオメトリの影ほど暗く落とさない）
        if (useCloudShadow)
        {
            float cloudShadow = SampleCloudShadow(gCloudShadowMap, gSampler, worldPos, L, gCloudShadow);
            shadowFactor *= lerp(1.0f, cloudShadow, gCloudShadow.sceneStrength);
        }

        float3 dirContribution =
            CalculatePBRLighting(N, V, L, dL.color.rgb, dL.intensity, albedo, metallic, roughness, ao)
            * shadowFactor;

        if (di == 0)
        {
            // メインライト（コースティクスの光源）: 水中では透過直接光の全量を
            // コースティクスが持つため、通常の直接光はクロスフェードで消す。
            replacedMainLight = dirContribution * underwaterFactor;
            dirContribution *= (1.0f - underwaterFactor);
        }
        else
        {
            // コースティクス非対象の補助ディレクショナルライトは透過率減衰のみ（近似）
            dirContribution *= underwaterAmbientT;
        }

        Lo += dirContribution;
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
        // 物理ベース: 純粋な逆二乗則（intensity は光度 [cd] 由来。最小距離 10cm でクランプ）
        float attenuation = 1.0f / max(d * d, 0.01f);
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
        // 物理ベース: 純粋な逆二乗則（ポイントライトと同一式）
        float attenuation = 1.0f / max(d * d, 0.01f);
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

        // 物理ベース: 純粋な逆二乗則 + 範囲窓関数（intensity は 輝度[nt]×面積 由来）
        float aDistRatio = dist / aL.range;
        float aRangeFactor = saturate(1.0f - aDistRatio * aDistRatio * aDistRatio * aDistRatio);
        float distAttenuation = aRangeFactor * aRangeFactor / max(dist * dist, 0.01f);

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
        //   テンポラル蓄積済みのRTシャドウをそのまま使用する（既に十分滑らか）。
        //   RTシャドウ未使用時（未ディスパッチ・非DXR環境）は影なし。
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
                    iblShadow = gRTShadowMask0.Load(loadCoord).r;
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
                float ambientShadow = 1.0f;
                if (useRTShadow)
                {
                    // テンポラル蓄積済みのRTシャドウは既に十分滑らかなので、追加ブラーは不要
                    if      (hli == 0) ambientShadow = gRTShadowMask0.Load(loadCoord).r;
                    else if (hli == 1) ambientShadow = gRTShadowMask1.Load(loadCoord).r;
                    else if (hli == 2) ambientShadow = gRTShadowMask2.Load(loadCoord).r;
                    else               ambientShadow = gRTShadowMask3.Load(loadCoord).r;
                }
                ambientShadow = lerp(0.3f, 1.0f, ambientShadow);

                ambient += CalculateHalfLambertAmbient(N, L, hL.color.rgb, hL.intensity, albedo, metallic, ao) * ambientShadow;
            }
        }

        // 水中では空・IBL アンビエントも水柱を通過して届くため Beer–Lambert で減衰させる
        // （下向き拡散光の平均光路 ≈ 鉛直水深の近似）。深いほど赤から失われ青緑へ転ぶ。
        ambient *= underwaterAmbientT;
    }

    // HDR値をそのまま出力（トーンマッピングはポストエフェクトチェーンで適用）
    // 合成式の物理的根拠（×kD×albedo/PI・水上影の引き継ぎ）は UnderwaterLighting.hlsli を参照。
    float3 waterCaustics = 0.0f.xxx;
    if (hasWaterCaustics)
    {
        if (gWaterCausticsDebug.waterVolumeEnabled != 0)
        {
            float causticsShadow = 1.0f;
            if (useRTShadow)
            {
                causticsShadow = gRTShadowMask0.Load(loadCoord).r;
            }
            waterCaustics = CompositeUnderwaterCaustics(
                waterCausticsSample.rgb, albedo, F0, metallic, causticsShadow);
        }
        else
        {
            // レガシー合成（スクリーンスペース版バックエンド等、置換モード外）
            waterCaustics = CompositeLegacyCaustics(waterCausticsSample.rgb, albedo, ao, metallic);
        }
    }

    // ===== 波打ち際アーティファクトの切り分け用可視化（モード 3/4）=====
    // 「水中ライティング置換」が絡む値なので、GBuffer が揃った最後で評価する
    // （モード 1/2 は生のコースティクス入力なのでシェーダー冒頭で処理している）。
    {
        bool debugHandled = false;
        const float4 debugColor = BuildUnderwaterDebugColor(
            gWaterCausticsDebug.debugViewMode,
            underwater,
            waterCausticsSample,
            replacedMainLight,
            waterCaustics,
            debugHandled);
        if (debugHandled)
        {
            output.color = debugColor;
            return output;
        }
    }

    float3 color = Lo + ambient + emissive + waterCaustics;
    output.color = float4(color, 1.0f);

    return output;
}
