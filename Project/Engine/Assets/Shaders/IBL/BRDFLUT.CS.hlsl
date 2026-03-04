/// @file BRDFLUT.CS.hlsl
/// @brief BRDF統合マップ（Look-Up Table）の事前計算
/// @details Cook-Torrance BRDFの積分をモンテカルロ法で計算し、
///          2Dテクスチャ(NdotV, roughness) -> (scale, bias)として保存

static const float PI = 3.14159265359;
static const uint SAMPLE_COUNT = 1024u;

// 出力テクスチャ（RG16F: R=scale, G=bias）
RWTexture2D<float2> gOutputBRDFLUT : register(u0);

// ===================================================================
// Hammersley サンプリング（低食い違い列）
// ===================================================================
/// @brief Van der Corput ビット反転
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

/// @brief Hammersley点列生成
float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

// ===================================================================
// GGX Importance Sampling
// ===================================================================
/// @brief GGX分布に基づくハーフベクトルのサンプリング
/// @param Xi ランダム値 [0,1]^2
/// @param N 法線ベクトル
/// @param roughness 粗さ
/// @return サンプリングされたハーフベクトル
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    
    // 球面座標での角度計算
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    
    // タンジェント空間でのハーフベクトル
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
    
    // タンジェント空間からワールド空間への変換
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    
    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

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
