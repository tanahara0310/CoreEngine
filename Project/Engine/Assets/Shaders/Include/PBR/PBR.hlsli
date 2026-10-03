/// @file PBR.hlsli
/// @brief 物理ベースレンダリング (PBR) の BRDF 計算関数
/// @details Cook-Torrance BRDF モデルを使用した PBR 実装

#ifndef PBR_HLSLI
#define PBR_HLSLI

#include "Lighting.hlsli" // LightingResult構造体の定義
#include "ShaderMath.hlsli" // PI / EPSILON などの共通数学定数

// ===================================================================
// PBR 定数
// ===================================================================
/// @brief Roughnessの最小値（0除算防止とシェーディング安定化）
static const float MIN_ROUGHNESS = 0.01f;

/// @brief F0の非金属デフォルト値（一般的な誘電体の反射率）
static const float DIELECTRIC_F0 = 0.04f;

/// @brief 解析光源の視半径の sin（太陽・月の視半径 0.27° 相当）
static const float LIGHT_SOURCE_SIN_ALPHA = 0.0047f;

// ===================================================================
// D項: GGX/Trowbridge-Reitz 法線分布関数 (Normal Distribution Function)
// ===================================================================
/// @brief マイクロファセットの分布を計算（GGX分布）
/// @param NdotH 法線とハーフベクトルの内積
/// @param alpha2 GGX の α²（α = roughness²）
/// @return 法線分布の強度
float DistributionGGX(float NdotH, float alpha2)
{
	float a2 = max(alpha2, 1.0e-7f);
	float NdotH2 = saturate(NdotH * NdotH);
	float denom = (1.0f - NdotH2) + NdotH2 * a2;
	return a2 / (PI * denom * denom);
}

/// @brief 光源の視半径の分だけ GGX の α² を広げる
/// @param alpha2 GGX の α²
/// @param HdotV ハーフベクトルと視線方向の内積
/// @return 広げた α²
float WidenAlpha2ForLightSource(float alpha2, float HdotV)
{
	return alpha2 + 0.25f * LIGHT_SOURCE_SIN_ALPHA * LIGHT_SOURCE_SIN_ALPHA / (HdotV + 0.001f);
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
	// pow(x,5) より乗算展開の方が精度・速度ともに優れる
	float f = saturate(1.0f - cosTheta);
	float f2 = f * f;
	return F0 + (1.0f - F0) * f2 * f2 * f;
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
    
    // D項: 法線分布関数（GGX）。ハイライトは光源の視半径より細くしない
	float a = roughness * roughness;
	float D = DistributionGGX(NdotH, WidenAlpha2ForLightSource(a * a, HdotV));
    
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
    // 非金属（誘電体）: DIELECTRIC_F0 (0.04 = 一般的な誘電体の反射率)
    // 金属: albedoそのもの
	float3 F0 = float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0);
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
	// AO（環境遮蔽）は間接光（環境光）にのみ適用すべきであり、直接光には乗算しない
	float3 radiance = lightColor * lightIntensity;
	float3 Lo = (diffuse + specular) * radiance * NdotL;

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
	float3 F0 = float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0); // 非金属のデフォルト値（0.04）
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
    
	// 距離減衰計算（物理ベース: 純粋な逆二乗則。intensity は光度 [cd] 由来のシェーダー単位）
	// 至近距離の発散は最小距離 10cm（d²=0.01）でクランプする。decay は旧仕様の名残で未使用
	float attenuation = 1.0f / max(distance * distance, 0.01f);

	// 半径外カットオフ: UE4/Filament 方式 (1-(d/r)^4)^2 で滑らかに消失
	float distRatio = distance / radius;
	float rangeFactor = saturate(1.0f - distRatio * distRatio * distRatio * distRatio);
	attenuation *= rangeFactor * rangeFactor;
    
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
    
	// 距離減衰（物理ベース: 純粋な逆二乗則。decay は旧仕様の名残で未使用）
	float attenuation = 1.0f / max(dist * dist, 0.01f);
	// UE4/Filament 方式 (1-(d/r)^4)^2 で滑らかに消失
	float distRatio = dist / distance;
	float rangeFactor = saturate(1.0f - distRatio * distRatio * distRatio * distRatio);
	attenuation *= rangeFactor * rangeFactor;

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
// 空アンビエント（大気散乱の SH9）と解析的 EnvBRDF
// ===================================================================
/// @brief 解析的 EnvBRDF 近似（Karis "Physically Based Shading on Mobile"）
/// @details Split-Sum の BRDF 積分項を LUT 無しで近似する
float2 EnvBRDFApprox(float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

/// @brief SH9 係数から法線方向の空の放射照度/π を評価する
/// @param coefficients 放射照度畳み込み済みの SH9 係数（rgb）
float3 EvaluateSkyIrradianceSH9(StructuredBuffer<float4> coefficients, float3 n)
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
        irradiance += coefficients[i].rgb * basis[i];
    }
    // SH の帯域打ち切りによる負のリンギングを防ぐ
    return max(irradiance, 0.0f);
}

// ===================================================================
// ハーフランバートアンビエント計算（空アンビエントが無いときのフォールバック）
// ===================================================================
/// @brief ハーフランバートによるアンビエントライティングを計算
/// @details 大気の空アンビエントが無い環境で、ディレクショナルライトを利用した
///          ソフトな環境光を提供する。NdotL を [0,1] ではなく [0.5,1] に
///          マッピングすることで裏面にも最低限の明るさを与える。
/// @param N 法線ベクトル（正規化済み）
/// @param L ライトへ向かう方向ベクトル（normalize(-lightDirection)）
/// @param lightColor ライトの色
/// @param lightIntensity ライトの強度
/// @param albedo アルベド（基本色）
/// @param metallic 金属性 (0.0-1.0)
/// @param ao 環境遮蔽 (0.0-1.0)
/// @return ハーフランバートアンビエント色
float3 CalculateHalfLambertAmbient(
    float3 N,
    float3 L,
    float3 lightColor,
    float lightIntensity,
    float3 albedo,
    float metallic,
    float ao)
{
    // ハーフランバート: NdotL を [−1,1] → [0.5,1] にリマップ
    float halfLambert = dot(N, L) * 0.5f + 0.5f;
    // 金属は拡散反射しない
    float3 diffuse = albedo * (1.0f - metallic);
    return diffuse * lightColor * lightIntensity * halfLambert * ao;
}

// ===================================================================
// 従来シェーディング: ランバート直接光
// ===================================================================
/// @brief 従来のランバート拡散反射（スペキュラなし）
/// @param N 法線ベクトル（正規化済み）
/// @param L ライトへ向かう方向（normalize(-lightDirection)）
/// @param lightColor ライトの色
/// @param lightIntensity ライトの強度
/// @param albedo アルベド
/// @param ao 環境遮蔽
/// @return ランバート拡散反射色
float3 CalculateLambertDiffuse(
    float3 N, float3 L,
    float3 lightColor, float lightIntensity,
    float3 albedo, float ao)
{
    float lambert = max(dot(N, L), 0.0f);
    return albedo * lightColor * lightIntensity * lambert * ao;
}

// ===================================================================
// 従来シェーディング: ハーフランバート直接光
// ===================================================================
/// @brief 従来のハーフランバート拡散反射（スペキュラなし）
/// @param N 法線ベクトル（正規化済み）
/// @param L ライトへ向かう方向（normalize(-lightDirection)）
/// @param lightColor ライトの色
/// @param lightIntensity ライトの強度
/// @param albedo アルベド
/// @param ao 環境遮蔽
/// @return ハーフランバート拡散反射色
float3 CalculateHalfLambertDiffuse(
    float3 N, float3 L,
    float3 lightColor, float lightIntensity,
    float3 albedo, float ao)
{
    float halfLambert = dot(N, L) * 0.5f + 0.5f;
    return albedo * lightColor * lightIntensity * halfLambert * ao;
}

#endif // PBR_HLSLI
