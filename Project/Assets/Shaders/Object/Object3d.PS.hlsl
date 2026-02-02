#include "Object3d.hlsli"
#include "../Common/LightStructures.hlsli"
#include "../Common/ShadowCalculation.hlsli"
#include "../Common/PBR.hlsli" // PBR.hlsliが内部でLighting.hlsliをインクルード

//マテリアル
struct Material
{
	float4 color;
	int enableLighting;
	float4x4 uvTransform;
	float shininess;
	int shadingMode; // 0: None, 1: Lambert, 2: Half-Lambert, 3: Toon
	float toonThreshold; // トゥーンシェーディングの閾値
	float toonSmoothness; // トゥーンシェーディングの滑らかさ
	int enableDithering; // ディザリング有効化フラグ
	float ditheringScale; // ディザリングのスケール（パターンの大きさ調整）
	int enableEnvironmentMap; // 環境マップ有効化フラグ
	float environmentMapIntensity; // 環境マップの反射強度
    
    // ===== PBR Parameters =====
	float metallic; // 金属性 (0.0 = 非金属, 1.0 = 金属)
	float roughness; // 粗さ (0.0 = 滑らか, 1.0 = 粗い)
	float ao; // Ambient Occlusion (環境遮蔽: 0.0 = 完全遮蔽, 1.0 = 遮蔽なし)
	int enablePBR; // PBR有効化フラグ (0: 無効(従来のライティング), 1: 有効)
    
    // ===== PBR Texture Map Flags =====
	int useNormalMap; // 法線マップ使用フラグ (0: 使用しない, 1: 使用する)
	int useMetallicMap; // メタリックマップ使用フラグ (0: 使用しない, 1: 使用する)
	int useRoughnessMap; // ラフネスマップ使用フラグ (0: 使用しない, 1: 使用する)
	int useAOMap; // AOマップ使用フラグ (0: 使用しない, 1: 使用する)
    
    // ===== IBL Parameters =====
	int enableIBL; // IBL有効化フラグ (0: 無効, 1: 有効)
	float iblIntensity; // IBL強度 (0.0-1.0, デフォルト: 1.0)
};

//カメラ
struct Camera
{
	float3 worldPosition;
};

// ===== ConstantBuffer =====
ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<LightCounts> gLightCounts : register(b1);

// ===== Texture & Sampler =====
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// ===== StructuredBuffer (Lights) =====
StructuredBuffer<DirectionalLightData> gDirectionalLights : register(t1);
StructuredBuffer<PointLightData> gPointLights : register(t2);
StructuredBuffer<SpotLightData> gSpotLights : register(t3);
StructuredBuffer<AreaLightData> gAreaLights : register(t4);

// ===== Environment Map =====
TextureCube<float4> gEnvironmentTexture : register(t5);

// ===== Shadow Map =====
Texture2D<float> gShadowMap : register(t6);
SamplerComparisonState gShadowSampler : register(s1);

// ===== PBR Texture Maps =====
Texture2D<float4> gNormalMap : register(t7); // 法線マップ (RGB: タンジェント空間法線)
Texture2D<float> gMetallicMap : register(t8); // メタリックマップ (R: 金属性)
Texture2D<float> gRoughnessMap : register(t9); // ラフネスマップ (R: 粗さ)
Texture2D<float> gAOMap : register(t10); // AOマップ (R: 環境遮蔽)

// ===== IBL Texture Maps =====
TextureCube<float4> gIrradianceMap : register(t11); // Irradianceマップ（拡散IBL）
TextureCube<float4> gPrefilteredMap : register(t12); // Prefilteredマップ（スペキュラIBL）
Texture2D<float2> gBRDFLUT : register(t13); // BRDF LUT（スペキュラIBL統合用）

// ディザリングパターン関数（4x4 Bayer Matrix）
float GetDitheringThreshold(float2 screenPos)
{
    // 4x4 Bayer Matrix (0-15の値を0-1に正規化)
	const float bayerMatrix[4][4] =
	{
		{ 0.0f / 16.0f, 8.0f / 16.0f, 2.0f / 16.0f, 10.0f / 16.0f },
		{ 12.0f / 16.0f, 4.0f / 16.0f, 14.0f / 16.0f, 6.0f / 16.0f },
		{ 3.0f / 16.0f, 11.0f / 16.0f, 1.0f / 16.0f, 9.0f / 16.0f },
		{ 15.0f / 16.0f, 7.0f / 16.0f, 13.0f / 16.0f, 5.0f / 16.0f }
	};
    
	int x = int(screenPos.x) % 4;
	int y = int(screenPos.y) % 4;
	return bayerMatrix[y][x];
}

/// @brief PBRパラメータを取得（テクスチャマップまたはマテリアル値から）
/// @param uv UV座標
/// @param outMetallic 出力：金属性 (0.0-1.0)
/// @param outRoughness 出力：粗さ (0.0-1.0)
/// @param outAO 出力：環境遮蔽 (0.0-1.0)
void GetPBRParameters(float2 uv, out float outMetallic, out float outRoughness, out float outAO)
{
    // Metallicの取得（マップがあればサンプル、なければマテリアル値）
	outMetallic = gMaterial.useMetallicMap != 0
        ? gMetallicMap.Sample(gSampler, uv)
        : gMaterial.metallic;
    
    // Roughnessの取得
	outRoughness = gMaterial.useRoughnessMap != 0
        ? gRoughnessMap.Sample(gSampler, uv)
        : gMaterial.roughness;
    
    // AOの取得
	outAO = gMaterial.useAOMap != 0
        ? gAOMap.Sample(gSampler, uv)
        : gMaterial.ao;
}

struct PixelShaderOutput
{
	float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
	PixelShaderOutput output;
    
    //テクスチャのUV座標を変換
	float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    
    //テクスチャをサンプリング
	float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    
    //カメラへの方向を算出
	float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
    
    // 最終的なアルファ値を計算
	float finalAlpha = gMaterial.color.a * textureColor.a;
    
    // ディザリングによる疑似透明化
	if (gMaterial.enableDithering != 0)
	{
        // スクリーン座標を取得（SV_POSITIONから）
		float2 screenPos = input.position.xy;
        
        // ディザリングのスケールを適用（パターンサイズ調整）
		if (gMaterial.ditheringScale > 0.0f)
		{
			screenPos *= gMaterial.ditheringScale;
		}
        
        // ディザリング閾値を取得（わずかなオフセットを加えてα=0で完全に消えるようにする）
		float threshold = GetDitheringThreshold(screenPos) + 0.001f;
        
        // アルファ値が閾値以下の場合、ピクセルを破棄
		if (finalAlpha <= threshold)
		{
			discard;
		}
        
	}
	else
	{
        // ディザリング無効時は従来のアルファテスト
		if (textureColor.a <= 0.5f)
		{
			discard;
		}
	}
   
    //Lightingする場合
	if (gMaterial.enableLighting != 0)
	{
        // シェーディングモードがNoneの場合
		if (gMaterial.shadingMode == 0)
		{
            // None（ライティングしない）→ 固有色×テクスチャだけ返す
			output.color = gMaterial.color * textureColor;
			return output;
		}
        
    // ライティング結果を累積
		float3 totalDiffuse = float3(0.0f, 0.0f, 0.0f);
		float3 totalSpecular = float3(0.0f, 0.0f, 0.0f);
    
    // ===== PBRパラメータの取得 =====
		float metallic, roughness, ao;
		if (gMaterial.enablePBR != 0)
		{
			GetPBRParameters(transformedUV.xy, metallic, roughness, ao);
			roughness = max(roughness, 0.01f);
		}
    
    // アルベド（基本色）の計算
		float3 albedo = gMaterial.color.rgb * textureColor.rgb;
        
    //==============================
    // ディレクショナルライトの計算（複数対応）
    //==============================
		for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
		{
			if (gDirectionalLights[i].enabled != 0)
			{
				LightingResult result;
            
				if (gMaterial.enablePBR != 0)
				{
					result = CalculateDirectionalLightPBR(
                    input.normal,
                    gDirectionalLights[i].direction,
                    gDirectionalLights[i].color.rgb,
                    gDirectionalLights[i].intensity,
                    toEye,
                    albedo,
                    metallic,
                    roughness,
                    ao
                );
				}
				else
				{
					result = CalculateDirectionalLight(
                    input.normal,
                    gDirectionalLights[i].direction,
                    gDirectionalLights[i].color.rgb,
                    gDirectionalLights[i].intensity,
                    toEye,
                    gMaterial.color.rgb,
                    textureColor,
                    gMaterial.shininess,
                    gMaterial.shadingMode,
                    gMaterial.toonThreshold
                );
				}
            
            
            // シャドウ適用
				float shadowFactor = CalculateShadow(
                input.lightSpacePos,
                input.normal,
                gDirectionalLights[i].direction,
                gShadowMap,
                gShadowSampler
            );
            
            // 影を自然にするため、完全に暗くならないように調整
            // shadowFactor: 0.0(完全な影) -> 0.3(影の明るさ)
            //               1.0(影なし) -> 1.0(そのまま)
				float shadowIntensity = 0.3f; // 影の最小明るさ（0.0で真っ黒、1.0で影なし）
				shadowFactor = lerp(shadowIntensity, 1.0f, shadowFactor);
        
				totalDiffuse += result.diffuse * shadowFactor;
				totalSpecular += result.specular;
			}
		}
        
        //==============================
        // ポイントライトの計算（複数対応）
        //==============================
		for (uint j = 0; j < gLightCounts.pointLightCount; ++j)
		{
			if (gPointLights[j].enabled != 0)
			{
				LightingResult result;
            
				if (gMaterial.enablePBR != 0)
				{
					result = CalculatePointLightPBR(
                    input.normal,
                    gPointLights[j].position,
                    input.worldPosition,
                    gPointLights[j].color.rgb,
                    gPointLights[j].intensity,
                    gPointLights[j].radius,
                    gPointLights[j].decay,
                    toEye,
                    albedo,
                    metallic,
                    roughness,
                    ao
                );
				}
				else
				{
					result = CalculatePointLight(
                    input.normal,
                    gPointLights[j].position,
                    input.worldPosition,
                    gPointLights[j].color.rgb,
                    gPointLights[j].intensity,
                    gPointLights[j].radius,
                    gPointLights[j].decay,
                    toEye,
                    gMaterial.color.rgb,
                    textureColor,
                    gMaterial.shininess,
                    gMaterial.shadingMode,
                    gMaterial.toonThreshold
                );
				}
				totalDiffuse += result.diffuse;
				totalSpecular += result.specular;
			}
		}
        
        //==============================
        // スポットライトの計算（複数対応）
        //==============================
		for (uint k = 0; k < gLightCounts.spotLightCount; ++k)
		{
			if (gSpotLights[k].enabled != 0)
			{
				LightingResult result;
            
				if (gMaterial.enablePBR != 0)
				{
					result = CalculateSpotLightPBR(
                    input.normal,
                    gSpotLights[k].position,
                    gSpotLights[k].direction,
                    input.worldPosition,
                    gSpotLights[k].color.rgb,
                    gSpotLights[k].intensity,
                    gSpotLights[k].distance,
                    gSpotLights[k].decay,
                    gSpotLights[k].cosAngle,
                    gSpotLights[k].cosFalloffStart,
                    toEye,
                    albedo,
                    metallic,
                    roughness,
                    ao
                );
				}
				else
				{
					result = CalculateSpotLight(
                    input.normal,
                    gSpotLights[k].position,
                    gSpotLights[k].direction,
                    input.worldPosition,
                    gSpotLights[k].color.rgb,
                    gSpotLights[k].intensity,
                    gSpotLights[k].distance,
                    gSpotLights[k].decay,
                    gSpotLights[k].cosAngle,
                    gSpotLights[k].cosFalloffStart,
                    toEye,
                    gMaterial.color.rgb,
                    textureColor,
                    gMaterial.shininess,
                    gMaterial.shadingMode,
                    gMaterial.toonThreshold
                );
				}
				totalDiffuse += result.diffuse;
				totalSpecular += result.specular;
			}
		}

        //==============================
        // エリアライトの計算（複数対応）
        //==============================
		for (uint l = 0; l < gLightCounts.areaLightCount; ++l)
		{
			if (gAreaLights[l].enabled != 0)
			{
				LightingResult result = CalculateAreaLight(
                    input.normal,
                    gAreaLights[l].position,
                    gAreaLights[l].normal,
                    gAreaLights[l].right,
                    gAreaLights[l].up,
                    gAreaLights[l].width,
                    gAreaLights[l].height,
                    input.worldPosition,
                    gAreaLights[l].color.rgb,
                    gAreaLights[l].intensity,
                    gAreaLights[l].range,
                    toEye,
                    gMaterial.color.rgb,
                    textureColor,
                    gMaterial.shininess,
                    gMaterial.shadingMode,
                    gMaterial.toonThreshold
                );
				totalDiffuse += result.diffuse;
				totalSpecular += result.specular;
			}
		}

        //==============================
        // ライティング結果の合成
        //==============================
		output.color.rgb = totalDiffuse + totalSpecular;
		output.color.a = gMaterial.color.a * textureColor.a; //アルファ値はそのまま保持
    //==============================
    // 環境マップの追加
    //==============================
		if (gMaterial.enableEnvironmentMap != 0)
		{
            // カメラから見た入射光ベクトル（ワールド座標でのピクセル位置 - カメラ位置）
			float3 cameraToPosition = normalize(input.worldPosition - gCamera.worldPosition);
            
            // 法線で反射させて、入射光を求める
			float3 reflectedVector = reflect(cameraToPosition, normalize(input.normal));
            
            // 環境マップをサンプリング
			float4 environmentColor = gEnvironmentTexture.Sample(gSampler, reflectedVector);
            
        // 環境マップの色を反射強度で合成
			if (gMaterial.enablePBR != 0)
			{
            // PBRパラメータを再取得（環境マップ処理用）
				float envMetallic, envRoughness, envAO;
				GetPBRParameters(transformedUV.xy, envMetallic, envRoughness, envAO);
            
            // metallicが高いほど環境マップの反射が強くなる（物理的に正しい）
            // roughnessが低いほど鮮明な反射
				float envFactor = gMaterial.environmentMapIntensity * envMetallic * (1.0f - envRoughness * 0.7f);
				output.color.rgb += environmentColor.rgb * envFactor;
			}
			else
			{
				output.color.rgb += environmentColor.rgb * gMaterial.environmentMapIntensity;
			}
		}

    //==============================
    // IBL（Image-Based Lighting）の追加
    //==============================
		if (gMaterial.enableIBL != 0 && gMaterial.enablePBR != 0)
		{
            // 視線方向
			float3 V = normalize(toEye);
			float3 N = normalize(input.normal);
            
		// PBRパラメータを取得
		float iblMetallic, iblRoughness, iblAO;
		GetPBRParameters(transformedUV.xy, iblMetallic, iblRoughness, iblAO);
		
		// 完全なIBL計算（Diffuse + Specular）
		float3 iblColor = CalculateFullIBL(
			N,
			V,
			albedo,
			iblMetallic,
			iblRoughness,
			iblAO,
			gIrradianceMap,
			gPrefilteredMap,
			gBRDFLUT,
			gSampler
		);
		
		// IBL強度を適用して加算
		output.color.rgb += iblColor * gMaterial.iblIntensity;
	}

	}
	else // Lightingしない場合
	{
		output.color = gMaterial.color * textureColor;
	}
    
    // ディザリング使用時はアルファを1.0に固定（Zソート不要）
	if (gMaterial.enableDithering != 0)
	{
		output.color.a = 1.0f;
	}
    
	return output;
}
