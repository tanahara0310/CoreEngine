#include "Object3d.hlsli"

// ハイブリッドレンダリングの GBuffer 書き込み専用シェーダー。
// 不透明モデル（BlendMode::None）の全てのメッシュがこのパスを通る。
// ライティングは DeferredLighting.PS.hlsl で行う。

struct Material
{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
    float metallic;
    float roughness;
    float ao;
    int useNormalMap;
    int useMetallicMap;
    int useRoughnessMap;
    int useAOMap;
    int enableDithering;
    float ditheringScale;
    int enableIBL;
    float iblIntensity;
    float padding2; ///< アライメント用
};

ConstantBuffer<Material> gMaterial : register(b0);

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
Texture2D<float4> gNormalMap : register(t7);
Texture2D<float> gMetallicMap : register(t8);
Texture2D<float> gRoughnessMap : register(t9);
Texture2D<float> gAOMap : register(t10);

struct GBufferOutput
{
    float4 albedoAO : SV_TARGET0; ///< rgb=アルベド, a=AO
    float4 normalRoughness : SV_TARGET1; ///< rgb=ワールド法線(エンコード済み), a=ラフネス
    float4 emissiveMetallic : SV_TARGET2; ///< rgb=エミッシブ, a=メタリック
    float4 worldPosition : SV_TARGET3; ///< rgb=ワールド座標, a=1.0f
};

float GetDitheringThreshold(float2 screenPos)
{
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

void GetPBRParameters(float2 uv, out float outMetallic, out float outRoughness, out float outAO)
{
    outMetallic = gMaterial.useMetallicMap != 0
        ? gMetallicMap.Sample(gSampler, uv)
        : gMaterial.metallic;

    outRoughness = gMaterial.useRoughnessMap != 0
        ? gRoughnessMap.Sample(gSampler, uv)
        : gMaterial.roughness;

    outAO = gMaterial.useAOMap != 0
        ? gAOMap.Sample(gSampler, uv)
        : gMaterial.ao;
}

float3 GetNormalFromMap(VertexShaderOutput input, float2 uv)
{
    if (gMaterial.useNormalMap == 0)
    {
        return normalize(input.normal);
    }

    float3 normalMapSample = gNormalMap.Sample(gSampler, uv).rgb;
    float3 tangentSpaceNormal = normalMapSample * 2.0f - 1.0f;

    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent);
    float3 B = normalize(input.bitangent);

    T = normalize(T - dot(T, N) * N);
    B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);
    return normalize(mul(tangentSpaceNormal, TBN));
}

GBufferOutput main(VertexShaderOutput input)
{
    GBufferOutput output;

    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float2 uv = transformedUV.xy;
    float4 textureColor = gTexture.Sample(gSampler, uv);
    float finalAlpha = gMaterial.color.a * textureColor.a;

    if (gMaterial.enableDithering != 0)
    {
        float2 screenPos = input.position.xy;
        if (gMaterial.ditheringScale > 0.0f)
        {
            screenPos *= gMaterial.ditheringScale;
        }

        float threshold = GetDitheringThreshold(screenPos) + 0.001f;
        if (finalAlpha <= threshold)
        {
            discard;
        }
    }
    else if (textureColor.a <= 0.5f)
    {
        discard;
    }

    float3 albedo = saturate((gMaterial.color * textureColor).rgb);

    // ===== アンリットマテリアル処理 =====
    // enableLighting=0 の場合、DeferredLighting パスに roughness=0 のセンチネル値を書き込む。
    // emissiveMetallic.rgb にアンリットカラーを格納し、DeferredLighting 側で検出して PBR をスキップする。
    if (gMaterial.enableLighting == 0)
    {
        output.albedoAO        = float4(0.0f, 0.0f, 0.0f, 1.0f);
        output.normalRoughness = float4(0.5f, 0.5f, 1.0f, 0.0f); // roughness=0 = アンリットセンチネル
        output.emissiveMetallic = float4(albedo, 0.0f);            // rgb にアンリットカラーを格納
        output.worldPosition   = float4(input.worldPosition, 1.0f);
        return output;
    }

    // ===== PBR パス（常にここに到達） =====
    float3 worldNormal   = GetNormalFromMap(input, uv);
    float3 encodedNormal = worldNormal * 0.5f + 0.5f;

    float metallic  = 0.0f;
    float roughness = 1.0f;
    float ao        = 1.0f;
    GetPBRParameters(uv, metallic, roughness, ao);

    metallic  = saturate(metallic);
    roughness = saturate(max(roughness, 0.01f));
    ao        = saturate(ao);

    // worldPosition.a ピクセルフラグ:
    // 0 = 背景（クリア値）
    // 1 = アンリット（normalRoughness.a=0 のセンチネルで識別）
    // 2 = PBR + IBL 無効
    // 3 = PBR + IBL 有効
    float pixelFlag = (gMaterial.enableIBL != 0) ? 3.0f : 2.0f;

    output.albedoAO        = float4(albedo, ao);
    output.normalRoughness = float4(encodedNormal, roughness);
    output.emissiveMetallic = float4(0.0f, 0.0f, 0.0f, metallic);
    output.worldPosition   = float4(input.worldPosition, pixelFlag);

    return output;
}
