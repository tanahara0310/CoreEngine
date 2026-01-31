/// @file PrefilterEnvironment.CS.hlsl
/// @brief Prefiltered Environment Map生成用コンピュートシェーダー
/// @details 各roughnessレベルで環境マップをImportance Samplingでフィルタリング

// ===== 定数 =====
static const float PI = 3.14159265359f;
static const uint SAMPLE_COUNT = 1024u; // サンプル数（高品質）

// ===== SRV & UAV =====
TextureCube<float4> gEnvironmentMap : register(t0); // 入力環境マップ
RWTexture2DArray<float4> gPrefilteredMap : register(u0); // 出力プリフィルタマップ
SamplerState gSampler : register(s0);

// ===== Constant Buffer =====
cbuffer PrefilteredParams : register(b0)
{
    float roughness;    // 現在のroughnessレベル (0.0-1.0)
    uint mipLevel;      // 現在のミップレベル (0-4)
    uint2 outputSize;   // 出力サイズ
};

// ===== ユーティリティ関数 =====

/// @brief Van der Corput低不一致シーケンス
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

/// @brief Hammersley低不一致サンプリング
float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

/// @brief GGX Importance Sampling
/// @param Xi 2D乱数 (0-1)
/// @param N 法線ベクトル
/// @param roughness 粗さ
/// @return ハーフベクトルH
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    
    // 球面座標でのサンプリング
    float phi = 2.0f * PI * Xi.x;
    float cosTheta = sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
    
    // タンジェント空間でのハーフベクトル
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
    
    // タンジェント空間 -> ワールド空間
    float3 up = abs(N.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    
    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

/// @brief GGX法線分布関数
float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;
    
    return nom / max(denom, 0.0001f);
}

/// @brief キューブマップ面IDと2D座標から方向ベクトルを計算
float3 GetCubemapDirection(uint faceIndex, float2 uv)
{
    // UV座標を-1～1に変換
    float2 texCoord = uv * 2.0f - 1.0f;
    
    float3 dir;
    switch (faceIndex)
    {
        case 0: dir = float3(1.0f, -texCoord.y, -texCoord.x); break; // +X
        case 1: dir = float3(-1.0f, -texCoord.y, texCoord.x); break; // -X
        case 2: dir = float3(texCoord.x, 1.0f, texCoord.y); break; // +Y
        case 3: dir = float3(texCoord.x, -1.0f, -texCoord.y); break; // -Y
        case 4: dir = float3(texCoord.x, -texCoord.y, 1.0f); break; // +Z
        case 5: dir = float3(-texCoord.x, -texCoord.y, -1.0f); break; // -Z
        default: dir = float3(0.0f, 0.0f, 1.0f); break;
    }
    return normalize(dir);
}

// ===== メインコンピュートシェーダー =====
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // 出力範囲チェック
    if (DTid.x >= outputSize.x || DTid.y >= outputSize.y || DTid.z >= 6)
        return;
    
    // UV座標を計算（テクセル中心）
    float2 uv = (float2(DTid.xy) + 0.5f) / float2(outputSize);
    
    // キューブマップの方向ベクトル（法線N）
    float3 N = GetCubemapDirection(DTid.z, uv);
    
    // 反射ベクトルR = N（視線方向とNが一致すると仮定）
    float3 R = N;
    float3 V = R;
    
    // プリフィルタリング計算
    float totalWeight = 0.0f;
    float3 prefilteredColor = float3(0.0f, 0.0f, 0.0f);
    
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        // Hammersley低不一致サンプリング
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        
        // GGX Importance Samplingでハーフベクトルを生成
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0f * dot(V, H) * H - V);
        
        float NdotL = max(dot(N, L), 0.0f);
        if (NdotL > 0.0f)
        {
            // 環境マップから色をサンプリング
            // roughness > 0の場合、低いミップレベルからサンプル（事前ぼかし）
            float envMapMip = roughness * 4.0f; // ミップレベル0-4
            float3 envColor = gEnvironmentMap.SampleLevel(gSampler, L, envMapMip).rgb;
            
            // サンプルの重み付け
            prefilteredColor += envColor * NdotL;
            totalWeight += NdotL;
        }
    }
    
    // 正規化
    prefilteredColor = totalWeight > 0.0f ? prefilteredColor / totalWeight : prefilteredColor;
    
    // 出力
    gPrefilteredMap[DTid] = float4(prefilteredColor, 1.0f);
}
