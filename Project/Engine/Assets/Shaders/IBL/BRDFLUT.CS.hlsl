/// @file BRDFLUT.CS.hlsl
/// @brief BRDF統合マップ（Look-Up Table）の事前計算
/// @details Cook-Torrance BRDFの積分をモンテカルロ法で計算し、
///          2Dテクスチャ(NdotV, roughness) -> (scale, bias)として保存

#include "Sampling.hlsli" // PI / Hammersley / ImportanceSampleGGX

static const uint SAMPLE_COUNT = 1024u;

// 出力テクスチャ（RG16F: R=scale, G=bias）
RWTexture2D<float2> gOutputBRDFLUT : register(u0);

// ===================================================================
// Geometry関数（Smith's method）
// ===================================================================
float GeometrySchlickGGX(float NdotV, float roughness)
{
    // IBL用のk（直接光とは異なる）
    float a = roughness;
    float k = (a * a) / 2.0;
    
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return nom / denom;
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// ===================================================================
// BRDF積分（Split-Sum Approximationの第2項）
// ===================================================================
/// @brief BRDF積分の計算
/// @param NdotV 法線と視線方向の内積
/// @param roughness 粗さ
/// @return (scale, bias) - Fresnel項の係数
float2 IntegrateBRDF(float NdotV, float roughness)
{
    // 視線方向（タンジェント空間でZ軸方向）
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV); // sin(theta)
    V.y = 0.0;
    V.z = NdotV; // cos(theta)
    
    float A = 0.0;
    float B = 0.0;
    
    float3 N = float3(0.0, 0.0, 1.0);
    
    // モンテカルロ積分
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        // Hammersley点列でサンプリング
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        
        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);
        
        if (NdotL > 0.0)
        {
            // Geometry項
            float G = GeometrySmith(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);
            
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    
    return float2(A, B);
}

// ===================================================================
// Compute Shader メインエントリーポイント
// ===================================================================
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint width, height;
    gOutputBRDFLUT.GetDimensions(width, height);
    
    // 範囲外チェック
    if (DTid.x >= width || DTid.y >= height)
        return;
    
    // UV座標を計算（中心サンプリング）
    float2 uv = (float2(DTid.xy) + 0.5) / float2(width, height);
    
    // NdotV: [0, 1] - 縦軸
    // roughness: [0, 1] - 横軸
    float NdotV = uv.x;
    float roughness = uv.y;
    
    // BRDF積分を計算
    float2 integratedBRDF = IntegrateBRDF(NdotV, roughness);
    
    // 結果を書き込み（R=scale, G=bias）
    gOutputBRDFLUT[DTid.xy] = integratedBRDF;
}
