/// @file SkyEnvironmentPrefilter.CS.hlsl
/// @brief 空キューブマップ（空＋雲）を GGX プリフィルタしてスペキュラIBLミップ群を生成する
/// @details IBL/PrefilterEnvironment.CS.hlsl のランタイム軽量版。
///          - 雲アニメーション中は毎フレーム走るため、サンプル数を 64 に抑える
///            （入力の空は低周波なのでこれで十分。雲の高周波はミップのブラーで均される）
///          - α（雲透過率）も同じ重みでフィルタする。水面が「平面反射に雲を
///            ブレンドする不透明度」として使うため（Water.PS.hlsl 参照）。
///          評価側は mip = roughness × (ミップ数-1) で SampleLevel する。

static const float PI = 3.14159265359f;
static const uint SAMPLE_COUNT = 64u;

TextureCube<float4> gSkyCubemap : register(t0);   // 入力: 空＋雲キューブマップ（mip0 のみ）
RWTexture2DArray<float4> gSkySpecularMap : register(u0); // 出力: 対象ミップの 6 面
SamplerState gLUTSampler : register(s0);

cbuffer SkyPrefilterParams : register(b0)
{
    float gRoughness;   // 現在のミップに対応するラフネス (0.0-1.0)
    float gPad0;
    uint2 gOutputSize;  // 出力ミップの解像度
};

/// @brief Van der Corput 低不一致シーケンス
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

/// @brief GGX Importance Sampling（PrefilterEnvironment.CS.hlsl と同一）
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0f * PI * Xi.x;
    float cosTheta = sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up = abs(N.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

/// @brief キューブマップ面IDと2D座標から方向ベクトルを計算（DirectX標準座標系）
float3 GetCubemapDirection(uint faceIndex, float2 uv)
{
    float2 texCoord = uv * 2.0f - 1.0f;
    float3 dir;
    switch (faceIndex)
    {
        case 0: dir = float3(1.0f, -texCoord.y, -texCoord.x); break;  // +X
        case 1: dir = float3(-1.0f, -texCoord.y, texCoord.x); break;  // -X
        case 2: dir = float3(texCoord.x, 1.0f, texCoord.y); break;    // +Y
        case 3: dir = float3(texCoord.x, -1.0f, -texCoord.y); break;  // -Y
        case 4: dir = float3(texCoord.x, -texCoord.y, 1.0f); break;   // +Z
        case 5: dir = float3(-texCoord.x, -texCoord.y, -1.0f); break; // -Z
        default: dir = float3(0.0f, 1.0f, 0.0f); break;
    }
    return normalize(dir);
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= gOutputSize.x || dtid.y >= gOutputSize.y || dtid.z >= 6)
    {
        return;
    }

    const float2 uv = (float2(dtid.xy) + 0.5f) / float2(gOutputSize);
    const float3 N = GetCubemapDirection(dtid.z, uv);

    // mip0（鏡面）は入力をそのままコピーする
    const float MIN_ROUGHNESS = 0.01f;
    if (gRoughness < MIN_ROUGHNESS)
    {
        gSkySpecularMap[dtid] = gSkyCubemap.SampleLevel(gLUTSampler, N, 0.0f);
        return;
    }

    // R = N = V 近似（Split-Sum の標準前提）
    const float3 R = N;
    const float3 V = R;

    float4 prefiltered = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, gRoughness);
        float3 L = normalize(2.0f * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0f);
        if (NdotL > 0.0f)
        {
            // 入力は 1 ミップのみのため SampleLevel(…, 0) 固定。
            // 空は低周波・雲は後段ミップのブラーで均されるためエイリアシングは実用上出ない
            prefiltered += gSkyCubemap.SampleLevel(gLUTSampler, L, 0.0f) * NdotL;
            totalWeight += NdotL;
        }
    }

    gSkySpecularMap[dtid] = (totalWeight > 0.0f) ? prefiltered / totalWeight : prefiltered;
}
