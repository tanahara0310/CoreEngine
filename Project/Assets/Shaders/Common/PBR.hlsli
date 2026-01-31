/// @file PBR.hlsli
/// @brief 物理ベースレンダリング (PBR) の BRDF 計算関数
/// @details Cook-Torrance BRDF モデルを使用した PBR 実装

#ifndef PBR_HLSLI
#define PBR_HLSLI

#include "Lighting.hlsli" // LightingResult構造体の定義

static const float PI = 3.14159265359f;
static const float EPSILON = 0.00001f;

// ===================================================================
// D項: GGX/Trowbridge-Reitz 法線分布関数 (Normal Distribution Function)
// ===================================================================
/// @brief マイクロファセットの分布を計算（GGX分布）
/// @param NdotH 法線とハーフベクトルの内積
/// @param roughness 粗さ (0.0 = 滑らか, 1.0 = 粗い)
/// @return 法線分布の強度
float DistributionGGX(float NdotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH2 = NdotH * NdotH;
    
	float nom = a2;
	float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
	denom = PI * denom * denom;
    
	return nom / max(denom, EPSILON);
}

// ===================================================================
// G項: Smith's Schlick-GGX 幾何減衰関数 (Geometry Function)
// ===================================================================
/// @brief 単一方向の幾何減衰を計算（Schlick-GGX）
/// @param NdotV 法線と視線方向の内積
/// @param roughness 粗さ
/// @return 幾何減衰係数
float GeometrySchlickGGX(float NdotV, float roughness)
{
	float r = (roughness + 1.0f);
	float k = (r * r) / 8.0f;
    
	float nom = NdotV;
	float denom = NdotV * (1.0f - k) + k;
    
	return nom / max(denom, EPSILON);
}

/// @brief Smith's method による双方向の幾何減衰
/// @param NdotV 法線と視線方向の内積
/// @param NdotL 法線とライト方向の内積
/// @param roughness 粗さ
/// @return 幾何減衰係数
float GeometrySmith(float NdotV, float NdotL, float roughness)
{
	float ggx1 = GeometrySchlickGGX(NdotV, roughness);
	float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    
	return ggx1 * ggx2;
}

// ===================================================================
// F項: Fresnel-Schlick 近似 (Fresnel Equation)
// ===================================================================
/// @brief Fresnel反射を計算（Schlick近似）
/// @param cosTheta 視線とハーフベクトルの内積（またはNdotV）
/// @param F0 垂直入射時の反射率（金属: albedo, 非金属: 0.04）
/// @return Fresnel反射率
float3 FresnelSchlick(float cosTheta, float3 F0)
{
	return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

/// @brief Fresnel反射（粗さ考慮版）- IBL用
/// @param cosTheta 視線とハーフベクトルの内積
/// @param F0 垂直入射時の反射率
/// @param roughness 粗さ
/// @return Fresnel反射率
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	return F0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0)
        * pow(1.0f - cosTheta, 5.0f);
}

// ===================================================================
// Cook-Torrance BRDF 計算
// ===================================================================
/// @brief Cook-Torrance 鏡面反射 BRDF を計算
/// @param N 法線ベクトル（正規化済み）
/// @param V 視線方向ベクトル（正規化済み）
/// @param L ライト方向ベクトル（正規化済み）
/// @param roughness 粗さ (0.0-1.0)
/// @param F0 垂直入射時の反射率
/// @return 鏡面反射BRDF値
float3 CookTorranceBRDF(float3 N, float3 V, float3 L, float roughness, float3 F0)
{
    // ハーフベクトル計算
	float3 H = normalize(V + L);
    
    // 各種内積を計算
	float NdotV = max(dot(N, V), 0.0f);
	float NdotL = max(dot(N, L), 0.0f);
	float NdotH = max(dot(N, H), 0.0f);
	float HdotV = max(dot(H, V), 0.0f);
    
    // D項: 法線分布関数（GGX）
	float D = DistributionGGX(NdotH, roughness);
    
    // F項: Fresnel反射
	float3 F = FresnelSchlick(HdotV, F0);
    
    // G項: 幾何減衰
	float G = GeometrySmith(NdotV, NdotL, roughness);
    
    // Cook-Torrance BRDF = (D * F * G) / (4 * NdotV * NdotL)
	float3 numerator = D * F * G;
	float denominator = 4.0f * NdotV * NdotL;
	float3 specular = numerator / max(denominator, EPSILON);
    
	return specular;
}

// ===================================================================
// 拡散反射（Lambertian）
// ===================================================================
/// @brief Lambertian 拡散反射を計算
/// @param albedo アルベド（基本色）
/// @param metallic 金属性 (0.0-1.0)
/// @param F Fresnel項（エネルギー保存則のため）
/// @return 拡散反射色
float3 DiffuseLambertPBR(float3 albedo, float metallic, float3 F)
{
    // kD = エネルギー保存則により、鏡面反射の残り
	float3 kD = float3(1.0f, 1.0f, 1.0f) - F;
    
    // 金属は拡散反射しない
	kD *= (1.0f - metallic);
    
    // Lambertian拡散反射
	return kD * albedo / PI;
}

// ===================================================================
// 完全なPBRライティング計算（1つのライトに対して）
// ===================================================================
/// @brief PBRライティングを計算（単一ライトソース）
/// @param N 法線ベクトル（正規化済み）
/// @param V 視線方向ベクトル（正規化済み）
/// @param L ライト方向ベクトル（正規化済み）
/// @param lightColor ライトの色
/// @param lightIntensity ライトの強度
/// @param albedo アルベド（基本色）
/// @param metallic 金属性 (0.0-1.0)
/// @param roughness 粗さ (0.0-1.0)
/// @param ao 環境遮蔽 (0.0-1.0)
/// @return 最終的なライティング色
float3 CalculatePBRLighting(
    float3 N,
    float3 V,
    float3 L,
    float3 lightColor,
    float lightIntensity,
    float3 albedo,
    float metallic,
    float roughness,
    float ao)
{
    // F0計算（垂直入射時の反射率）
    // 非金属（誘電体）: 約0.04
    // 金属: albedoそのもの
	float3 F0 = float3(0.04f, 0.04f, 0.04f);
	F0 = lerp(F0, albedo, metallic);
    
    // ハーフベクトル
	float3 H = normalize(V + L);
	float HdotV = max(dot(H, V), 0.0f);
    
    // Cook-Torrance 鏡面反射BRDF
	float3 specular = CookTorranceBRDF(N, V, L, roughness, F0);
    
    // Fresnel項を再計算（エネルギー保存則のため）
	float3 F = FresnelSchlick(HdotV, F0);
    
    // Lambertian 拡散反射
	float3 diffuse = DiffuseLambertPBR(albedo, metallic, F);
    
    // N・L（ランバートコサイン則）
	float NdotL = max(dot(N, L), 0.0f);
    
    // 最終的な放射輝度
	float3 radiance = lightColor * lightIntensity;
	float3 Lo = (diffuse + specular) * radiance * NdotL;
    
    // 環境遮蔽を適用
	Lo *= ao;
    
	return Lo;
}

// ===================================================================
// F0計算ユーティリティ
// ===================================================================
/// @brief 垂直入射時の反射率（F0）を計算
/// @param albedo アルベド（基本色）
/// @param metallic 金属性 (0.0-1.0)
/// @return F0値
float3 CalculateF0(float3 albedo, float metallic)
{
	float3 F0 = float3(0.04f, 0.04f, 0.04f); // 非金属のデフォルト値
	return lerp(F0, albedo, metallic);
}

// ===================================================================
// PBR ディレクショナルライト計算
// ===================================================================
/// @brief PBRディレクショナルライトを計算
/// @param normal 法線ベクトル（正規化済み）
/// @param lightDirection ライト方向ベクトル（正規化済み）
/// @param lightColor ライトの色
/// @param intensity ライトの強度
/// @param toEye 視線方向ベクトル（正規化済み）
/// @param albedo アルベド（基本色）
/// @param metallic 金属性 (0.0-1.0)
/// @param roughness 粗さ (0.0-1.0)
/// @param ao 環境遮蔽 (0.0-1.0)
/// @return ライティング結果（diffuse + specular）
LightingResult CalculateDirectionalLightPBR(
    float3 normal,
    float3 lightDirection,
    float3 lightColor,
    float intensity,
    float3 toEye,
    float3 albedo,
    float metallic,
    float roughness,
    float ao)
{
	LightingResult result;
    
    // ライト方向を反転（ライトへ向かう方向に）
	float3 L = -normalize(lightDirection);
	float3 V = normalize(toEye);
	float3 N = normalize(normal);
    
    // PBRライティング計算
	float3 lighting = CalculatePBRLighting(
        N, V, L,
        lightColor,
        intensity,
        albedo,
        metallic,
        roughness,
        ao
    );
    
    // LightingResult形式に変換（diffuseとspecularは統合されている）
	result.diffuse = lighting;
	result.specular = float3(0.0f, 0.0f, 0.0f); // PBRでは既に統合済み
    
	return result;
}

// ===================================================================
// PBR ポイントライト計算
// ===================================================================
/// @brief PBRポイントライトを計算
/// @param normal 法線ベクトル（正規化済み）
/// @param lightPosition ライトのワールド座標
/// @param surfacePosition サーフェスのワールド座標
/// @param lightColor ライトの色
/// @param intensity ライトの強度
/// @param radius ライトの影響半径
/// @param decay 減衰係数
/// @param toEye 視線方向ベクトル（正規化済み）
/// @param albedo アルベド（基本色）
/// @param metallic 金属性 (0.0-1.0)
/// @param roughness 粗さ (0.0-1.0)
/// @param ao 環境遮蔽 (0.0-1.0)
/// @return ライティング結果
LightingResult CalculatePointLightPBR(
    float3 normal,
    float3 lightPosition,
    float3 surfacePosition,
    float3 lightColor,
    float intensity,
    float radius,
    float decay,
    float3 toEye,
    float3 albedo,
    float metallic,
    float roughness,
    float ao)
{
	LightingResult result;
    
    // サーフェスからライトへの方向と距離
	float3 lightVector = lightPosition - surfacePosition;
	float distance = length(lightVector);
	float3 L = normalize(lightVector);
	float3 V = normalize(toEye);
	float3 N = normalize(normal);
    
    // 距離減衰計算（物理ベース: 逆二乗則）
	float attenuation = 1.0f / (1.0f + decay * distance * distance);
    
    // 半径外は影響なし
	float rangeFactor = saturate(1.0f - (distance / radius));
	attenuation *= rangeFactor;
    
    // PBRライティング計算
	float3 lighting = CalculatePBRLighting(
        N, V, L,
        lightColor,
        intensity * attenuation,
        albedo,
        metallic,
        roughness,
        ao
    );
    
	result.diffuse = lighting;
	result.specular = float3(0.0f, 0.0f, 0.0f);
    
	return result;
}

// ===================================================================
// PBR スポットライト計算
// ===================================================================
/// @brief PBRスポットライトを計算
/// @param normal 法線ベクトル（正規化済み）
/// @param lightPosition ライトのワールド座標
/// @param lightDirection ライトの向き（正規化済み）
/// @param surfacePosition サーフェスのワールド座標
/// @param lightColor ライトの色
/// @param intensity ライトの強度
/// @param distance ライトの影響距離
/// @param decay 減衰係数
/// @param cosAngle スポットライトの外側のコサイン値
/// @param cosFalloffStart スポットライトの内側のコサイン値
/// @param toEye 視線方向ベクトル（正規化済み）
/// @param albedo アルベド（基本色）
/// @param metallic 金属性 (0.0-1.0)
/// @param roughness 粗さ (0.0-1.0)
/// @param ao 環境遮蔽 (0.0-1.0)
/// @return ライティング結果
LightingResult CalculateSpotLightPBR(
    float3 normal,
    float3 lightPosition,
    float3 lightDirection,
    float3 surfacePosition,
    float3 lightColor,
    float intensity,
    float distance,
    float decay,
    float cosAngle,
    float cosFalloffStart,
    float3 toEye,
    float3 albedo,
    float metallic,
    float roughness,
    float ao)
{
	LightingResult result;
    
    // サーフェスからライトへの方向と距離
	float3 lightVector = lightPosition - surfacePosition;
	float dist = length(lightVector);
	float3 L = normalize(lightVector);
	float3 V = normalize(toEye);
	float3 N = normalize(normal);
    
    // 距離減衰
	float attenuation = 1.0f / (1.0f + decay * dist * dist);
	float rangeFactor = saturate(1.0f - (dist / distance));
	attenuation *= rangeFactor;
    
    // スポットライトのコーン減衰
	float3 lightDirNorm = normalize(lightDirection);
	float cosTheta = dot(-L, lightDirNorm);
	float spotFactor = saturate((cosTheta - cosAngle) / (cosFalloffStart - cosAngle));
	attenuation *= spotFactor;
    
    // PBRライティング計算
	float3 lighting = CalculatePBRLighting(
        N, V, L,
        lightColor,
        intensity * attenuation,
        albedo,
        metallic,
        roughness,
        ao
    );
    
	result.diffuse = lighting;
	result.specular = float3(0.0f, 0.0f, 0.0f);
    
	return result;
}

// ===================================================================
// IBL（Image-Based Lighting）計算
// ===================================================================

/// @brief 拡散反射IBLを計算（Irradianceのみ）
/// @param N 法線ベクトル（正規化済み）
/// @param V 視線方向ベクトル（正規化済み）
/// @param albedo アルベド（基本色）
/// @param metallic 金属性 (0.0-1.0)
/// @param roughness 粗さ (0.0-1.0)
/// @param ao 環境遮蔽 (0.0-1.0)
/// @param irradianceMap Irradianceキューブマップ
/// @param samp サンプラー
/// @return 拡散IBL色
float3 CalculateIrradianceIBL(
    float3 N,
    float3 V,
    float3 albedo,
    float metallic,
    float roughness,
    float ao,
    TextureCube irradianceMap,
    SamplerState samp)
{
    // Irradianceマップから環境光を取得
	float3 irradiance = irradianceMap.Sample(samp, N).rgb;
    
    // F0計算（垂直入射時の反射率）
	float3 F0 = float3(0.04f, 0.04f, 0.04f);
	F0 = lerp(F0, albedo, metallic);
    
    // Fresnel項（環境光用、実際のroughnessを使用）
	float NdotV = max(dot(N, V), 0.0f);
	float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
    
    // 拡散反射成分（金属は拡散しない）
	float3 kD = (1.0f - F) * (1.0f - metallic);
	float3 diffuseIBL = kD * albedo * irradiance;
    
    // AOを適用
	return diffuseIBL * ao;
}

/// @brief スペキュラIBLを計算（Prefiltered Environment Map + BRDF LUT）
/// @param N 法線ベクトル（正規化済み）
/// @param V 視線方向ベクトル（正規化済み）
/// @param albedo アルベド（基本色）
/// @param metallic 金属性 (0.0-1.0)
/// @param roughness 粗さ (0.0-1.0)
/// @param ao 環境遮蔽 (0.0-1.0)
/// @param prefilteredMap Prefiltered Environment Mapキューブマップ
/// @param brdfLUT BRDF LUTテクスチャ（RG16形式）
/// @param samp サンプラー
/// @return スペキュラIBL色
float3 CalculateSpecularIBL(
    float3 N,
    float3 V,
    float3 albedo,
    float metallic,
    float roughness,
    float ao,
    TextureCube prefilteredMap,
    Texture2D<float2> brdfLUT,
    SamplerState samp)
{
    // F0計算（垂直入射時の反射率）
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = lerp(F0, albedo, metallic);
    
    // 反射ベクトルR（視線Vを法線Nで反射）
    float3 R = reflect(-V, N);
    
    // NdotV
    float NdotV = max(dot(N, V), 0.0f);
    
    // Fresnel-Schlick（roughness考慮版）
    float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
    
    // Prefiltered Mapからサンプリング（roughnessに応じたミップレベル）
    // roughness 0.0 -> mip 0, roughness 1.0 -> mip 4
    float mipLevel = roughness * 4.0f; // 5ミップレベル（0-4）
    float3 prefilteredColor = prefilteredMap.SampleLevel(samp, R, mipLevel).rgb;
    
    // BRDF LUTからサンプリング（NdotV, roughness）
    float2 envBRDF = brdfLUT.Sample(samp, float2(NdotV, roughness)).rg;
    
    // Split-Sum近似
    // SpecularIBL = PrefilteredColor * (F * scale + bias)
    float3 specularIBL = prefilteredColor * (F * envBRDF.x + envBRDF.y);
    
    // AOを適用
    return specularIBL * ao;
}

/// @brief 完全なIBLを計算（Diffuse + Specular）
/// @param N 法線ベクトル（正規化済み）
/// @param V 視線方向ベクトル（正規化済み）
/// @param albedo アルベド（基本色）
/// @param metallic 金属性 (0.0-1.0)
/// @param roughness 粗さ (0.0-1.0)
/// @param ao 環境遮蔽 (0.0-1.0)
/// @param irradianceMap Irradianceキューブマップ
/// @param prefilteredMap Prefiltered Environment Mapキューブマップ
/// @param brdfLUT BRDF LUTテクスチャ（RG16形式）
/// @param samp サンプラー
/// @return 完全なIBL色（Diffuse + Specular）
float3 CalculateFullIBL(
    float3 N,
    float3 V,
    float3 albedo,
    float metallic,
    float roughness,
    float ao,
    TextureCube irradianceMap,
    TextureCube prefilteredMap,
    Texture2D<float2> brdfLUT,
    SamplerState samp)
{
    // F0計算（垂直入射時の反射率）
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = lerp(F0, albedo, metallic);
    
    // NdotV
    float NdotV = max(dot(N, V), 0.0f);
    
    // Fresnel-Schlick（roughness考慮版）
    float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
    
    // === 拡散IBL（Irradiance Map） ===
    float3 irradiance = irradianceMap.Sample(samp, N).rgb;
    float3 kD = (1.0f - F) * (1.0f - metallic); // 金属は拡散しない
    float3 diffuseIBL = kD * albedo * irradiance;
    
    // === スペキュラIBL（Prefiltered Map + BRDF LUT） ===
    float3 R = reflect(-V, N);
    float mipLevel = roughness * 4.0f; // 5ミップレベル（0-4）
    float3 prefilteredColor = prefilteredMap.SampleLevel(samp, R, mipLevel).rgb;
    
    // BRDF LUTからサンプリング
    float2 envBRDF = brdfLUT.Sample(samp, float2(NdotV, roughness)).rg;
    
    // Split-Sum近似
    float3 specularIBL = prefilteredColor * (F * envBRDF.x + envBRDF.y);
    
    // 最終的なIBL = Diffuse + Specular
    float3 ibl = (diffuseIBL + specularIBL) * ao;
    
    return ibl;
}

#endif // PBR_HLSLI
