/// @file PrefilterEnvironment.CS.hlsl
/// @brief Prefiltered Environment Map生成用コンピュートシェーダー
/// @details 各roughnessレベルで環境マップをImportance Samplingでフィルタリング

#include "Sampling.hlsli" // PI / Hammersley / ImportanceSampleGGX
#include "Cubemap.hlsli"  // GetCubemapDirection

// ===== 定数 =====
static const uint SAMPLE_COUNT = 512u; // サンプル数（Hammersley低不一致列で512sppは十分な品質）

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
    
    // roughnessが0に近い場合は環境マップを直接サンプリング
    const float MIN_ROUGHNESS = 0.01f;
    if (roughness < MIN_ROUGHNESS)
    {
        prefilteredColor = gEnvironmentMap.SampleLevel(gSampler, N, 0.0f).rgb;
    }
    else
    {
        for (uint i = 0; i < SAMPLE_COUNT; ++i)
        {
            // Hammersley低不一致サンプリング
            float2 Xi = Hammersley(i, SAMPLE_COUNT);
            
            // GGX Importance Samplingでハーフベクトルを生成
            float3 H = ImportanceSampleGGX(Xi, N, roughness);
            float3 L = normalize(2.0f * dot(V, H) * H - V);
            
            float NdotL = max(dot(N, L), 0.0f);
            float NdotH = max(dot(N, H), 0.0f);
            float VdotH = max(dot(V, H), 0.0f);
            
            if (NdotL > 0.0f)
            {
                // GGX分布関数
                float D = DistributionGGX(NdotH, roughness);
                
                // 確率密度関数 (PDF)
                float pdf = (D * NdotH / (4.0f * VdotH)) + 0.0001f;
                
                // ソリッドアングル（1ピクセルあたり）
                float resolution = 1024.0f; // 入力環境マップの解像度
                float saTexel = 4.0f * PI / (6.0f * resolution * resolution);
                
                // サンプルのソリッドアングル
                float saSample = 1.0f / (float(SAMPLE_COUNT) * pdf + 0.0001f);
                
                // ミップレベル選択（テクスチャフィルタリング）
                float mipLevel = roughness == 0.0f ? 0.0f : 0.5f * log2(saSample / saTexel);
                mipLevel = max(0.0f, mipLevel);
                
                // 環境マップから色をサンプリング
                float3 envColor = gEnvironmentMap.SampleLevel(gSampler, L, mipLevel).rgb;
                
                // サンプルの重み付け（NdotL係数）
                prefilteredColor += envColor * NdotL;
                totalWeight += NdotL;
            }
        }
        
        // 正規化
        prefilteredColor = totalWeight > 0.0f ? prefilteredColor / totalWeight : prefilteredColor;
    }
    
    // 出力
    gPrefilteredMap[DTid] = float4(prefilteredColor, 1.0f);
}
