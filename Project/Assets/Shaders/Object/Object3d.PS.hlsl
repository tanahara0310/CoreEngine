#include "Object3d.hlsli"
#include "../Common/Lighting.hlsli"
#include "../Common/LightStructures.hlsli"

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
        
        //==============================
        // ディレクショナルライトの計算（複数対応）
        //==============================
        for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
        {
            if (gDirectionalLights[i].enabled != 0)
            {
                LightingResult result = CalculateDirectionalLight(
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
                totalDiffuse += result.diffuse;
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
                LightingResult result = CalculatePointLight(
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
                LightingResult result = CalculateSpotLight(
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
                totalDiffuse += result.diffuse;
                totalSpecular += result.specular;
            }
        }

        //==============================
        // ライティング結果の合成
        //==============================
        output.color.rgb = totalDiffuse + totalSpecular;
        output.color.a = gMaterial.color.a * textureColor.a; //アルファ値はそのまま保持

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