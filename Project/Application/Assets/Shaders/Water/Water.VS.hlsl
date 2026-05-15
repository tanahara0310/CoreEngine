#include "../../../../Engine/Assets/Shaders/Include/Object/Object3dVertex.hlsli"

// ===== Gerstner Wave 定数バッファ =====
// WaterPlaneObject::BindCustomResources() が b4 にバインドする
struct WaveParams
{
    float2 direction; // 進行方向（XZ, 正規化済み）
    float amplitude; // 振幅
    float wavelength; // 波長
    float speed; // 位相速度
    float steepness; // 横揺れ係数 Q（0=正弦波, 1=完全Gerstner）
    float2 padding;
};

cbuffer WaterConstants : register(b4)
{
    WaveParams gWaves[4]; // 重ね合わせる波（最大 4 本）
    float gTime; // 経過時間（秒）
    float3 gPadding;
};

/// @brief Gerstner Wave 1 本分の頂点変位を計算する
/// @param worldPos 変位前のワールド座標（XZ を参照、Y を更新）
/// @param wave     波パラメータ
/// @return 変位量（XYZ）
float3 CalcGerstnerOffset(float3 worldPos, WaveParams wave)
{
    // 波数 k = 2π / λ
    float k = 2.0f * 3.14159265f / wave.wavelength;

    // 角周波数 ω ≒ speed * k（簡易近似、本来は √(g*k)）
    float omega = wave.speed * k;

    // 位相 φ = k*(D・P) + ω*t
    float phase = k * dot(wave.direction, worldPos.xz) + omega * gTime;

    float sinP = sin(phase);
    float cosP = cos(phase);

    // Gerstner Wave 変位式
    float3 offset;
    offset.x = wave.steepness * wave.amplitude * wave.direction.x * cosP;
    offset.y = wave.amplitude * sinP;
    offset.z = wave.steepness * wave.amplitude * wave.direction.y * cosP;

    return offset;
}

VertexShaderOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    // ---- 1. ワールド変換 ----
    float4 worldPos4 = mul(input.position, mtx.World);
    float3 worldPos = worldPos4.xyz;

    // ---- 2. Gerstner Wave 頂点変位（ワールド空間） ----
    float3 totalOffset = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        totalOffset += CalcGerstnerOffset(worldPos, gWaves[i]);
    }
    worldPos += totalOffset;

    // ---- 3. 変位後の法線・接線を解析的に再計算（Gerstner の偏微分） ----
    float3 normal = float3(0.0f, 1.0f, 0.0f);
    float3 tangent = float3(1.0f, 0.0f, 0.0f);
    [unroll]
    for (int j = 0; j < 4; ++j)
    {
        float k = 2.0f * 3.14159265f / gWaves[j].wavelength;
        float omega = gWaves[j].speed * k;
        float phase = k * dot(gWaves[j].direction, worldPos.xz) + omega * gTime;
        float WA = k * gWaves[j].amplitude;
        float sinP = sin(phase);
        float cosP = cos(phase);

        // 法線偏微分
        normal.x -= gWaves[j].direction.x * WA * cosP;
        normal.y -= gWaves[j].steepness * WA * sinP;
        normal.z -= gWaves[j].direction.y * WA * cosP;

        // X 方向接線偏微分
        tangent.x -= gWaves[j].steepness * gWaves[j].direction.x * gWaves[j].direction.x * WA * sinP;
        tangent.y += gWaves[j].direction.x * WA * cosP;
        tangent.z -= gWaves[j].steepness * gWaves[j].direction.x * gWaves[j].direction.y * WA * sinP;
    }
    normal = normalize(normal);
    tangent = normalize(tangent);

    // ---- 4. 出力組み立て ----
    VertexShaderOutput output;
    output.texcoord = input.texcoord;

    // 変位ベクトルをローカル空間に逆変換してから WVP を通すことで
    // クリップ座標を正確に求める（World がスケール均一な場合に正確）
    float4 baseClip = mul(input.position, mtx.WVP);
    float3x3 invWorld3 = transpose((float3x3) mtx.World);
    float3 offsetLS = mul(totalOffset, invWorld3);
    float4 offsetClip = mul(float4(offsetLS, 0.0f), mtx.WVP);
    output.position = baseClip + offsetClip;

    output.normal = normalize(mul(normal, (float3x3) mtx.WorldInversTranspose));
    output.tangent = normalize(mul(tangent, (float3x3) mtx.World));
    output.bitangent = normalize(cross(output.normal, output.tangent));

    output.worldPosition = worldPos;
    output.lightSpacePos = mul(float4(worldPos, 1.0f), mtx.LightViewProjection);
    output.clipPosCurrent = output.position;
    output.clipPosPrev = mul(input.position, mtx.PrevWVP);

    return output;
}
