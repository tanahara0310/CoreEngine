// SSAO（Screen Space Ambient Occlusion）ピクセルシェーダー
//
// GBuffer の WorldPosition / NormalRoughness をベースに、
// 半球サンプリングで遮蔽率を計算する。
// 出力: 1.0 = 遮蔽なし（明るい）、0.0 = 完全遮蔽（暗い）

#include "FullScreen.hlsli"
#include "../Include/Common/DepthReconstruction.hlsli"

Texture2D<float4> gNormalRoughness : register(t0); // NormalRoughness
Texture2D<float> gSceneDepth       : register(t1); // WorldPosition ターゲット廃止に伴い深度から復元する

SamplerState gSampler : register(s0);

cbuffer SSAOParams : register(b0)
{
    float4x4 gView;
    float4x4 gProjection;
    float4x4 gInvViewProj;
    float    gRadius;
    float    gBias;
    float    gIntensity;
    int      gSampleCount;
    float2   gScreenSize;
    float    gPower;
    float    _pad0;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PixelShaderOutput
{
    float4 color : SV_Target;
};

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 HemisphereSampleCosine(float2 xi)
{
    float phi = 2.0f * 3.14159265f * xi.x;
    float cosTheta = sqrt(1.0f - xi.y);
    float sinTheta = sqrt(xi.y);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float3 BuildTBN(float3 N, float rotAngle, float3 v)
{
    float3 up = (abs(N.y) < 0.999f) ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);

    float c = cos(rotAngle);
    float s = sin(rotAngle);
    float3 Tr = T * c + B * s;
    float3 Br = -T * s + B * c;

    return Tr * v.x + Br * v.y + N * v.z;
}

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;

    int3 loadCoord = int3(input.position.xy, 0);
    float4 normalRoughness = gNormalRoughness.Load(loadCoord);
    float centerDepth = gSceneDepth.Load(loadCoord);

    // normalRoughness.a: 0.0=アンリット(センチネル), 符号=IBL有効/無効, 絶対値=roughness
    if (IsBackgroundDepth(centerDepth) || normalRoughness.a == 0.0f)
    {
        output.color = float4(1.0f, 1.0f, 1.0f, 1.0f);
        return output;
    }

    float2 centerUV = (input.position.xy + 0.5f.xx) / gScreenSize;
    float3 worldPos = ReconstructWorldPosition(ScreenUVToNDC(centerUV), centerDepth, gInvViewProj);
    float3 N        = normalize(normalRoughness.rgb * 2.0f - 1.0f);

    // サンプル回転のランダム化。
    //
    // 基準は「毎フレーム同じ値になる量」でなければならない。
    //   × ワールド座標: 深度から復元した worldPos は TAA のジッタで毎フレーム微妙に動き、
    //     ハッシュはカオス的（微小変化で出力が全く変わる）なので回転が毎フレーム
    //     完全ランダムになる ＝ AO がフレームごとに別ノイズ（ちかちか）
    //   × ジッタ補正したピクセル座標: floor の結果がジッタの正負で反転する画素が出る
    //   ○ 生のピクセル座標: フルスクリーンパスの SV_Position はシーンのジッタと無関係で、
    //     静止時は毎フレーム完全に同一。回転が固定されれば、残る変動は深度入力の
    //     サブピクセル変化だけになり、ブラーと TAA で吸収できる
    float randRot = Hash12(floor(input.position.xy)) * 6.28318531f;

    uint sampleCount = (uint)clamp(gSampleCount, 1, 64);

    float occlusion = 0.0f;
    float weightSum = 0.0f;

    [loop]
    for (uint i = 0; i < sampleCount; ++i)
    {
        float2 xi = Hammersley(i, sampleCount);
        float3 tangentSample = HemisphereSampleCosine(xi);

        float scale = float(i) / float(sampleCount);
        scale = lerp(0.1f, 1.0f, scale * scale);
        tangentSample *= scale * gRadius;

        float3 sampleWorld = worldPos + BuildTBN(N, randRot, tangentSample);

        float4 sampleView = mul(float4(sampleWorld, 1.0f), gView);
        float4 clip = mul(sampleView, gProjection);
        if (clip.w <= 0.0f) continue;
        float3 ndc = clip.xyz / clip.w;
        float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
        if (any(uv < 0.0f) || any(uv > 1.0f)) continue;

        int2 sampleCoord = int2(uv * gScreenSize);
        float sampleDepth = gSceneDepth.Load(int3(sampleCoord, 0));
        if (IsBackgroundDepth(sampleDepth)) continue;
        float3 sampleWorldPos = ReconstructWorldPosition(ndc.xy, sampleDepth, gInvViewProj);

        float sampleViewZ = sampleView.z;
        float realViewZ   = mul(float4(sampleWorldPos, 1.0f), gView).z;

        float rangeCheck = smoothstep(0.0f, 1.0f, gRadius / max(abs(sampleViewZ - realViewZ), 0.0001f));

        // 遮蔽の有無を 2 値で決めてはいけない。
        //
        // 深度入力がほんのわずか動いただけで 0↔1 が反転し、16 サンプル中 1 つ反転するだけで
        // AO が 1/16（6%）跳ねる。草の揺れ・波・サブピクセルのずれなど、入力が少しでも動けば
        // 出力がバイナリに暴れる ＝ 影の部分が毎フレームちらつく。
        //
        // バイアス幅で滑らかに立ち上げれば、入力の微小変化は出力の微小変化に収まる。
        // 遠いほど深度の量子化誤差が大きくなるため、しきい値も距離に比例させる。
        float biasScaled = gBias * max(1.0f, abs(sampleViewZ) * 0.05f);
        float depthDelta = (sampleViewZ - biasScaled) - realViewZ;
        float occluded   = smoothstep(0.0f, max(biasScaled * 2.0f, 1e-4f), depthDelta);

        occlusion += occluded * rangeCheck;
        weightSum += 1.0f;
    }

    float ao = (weightSum > 0.0f) ? (1.0f - occlusion / weightSum * gIntensity) : 1.0f;
    ao = saturate(ao);
    ao = pow(ao, max(gPower, 0.001f));

    output.color = float4(ao, ao, ao, 1.0f);
    return output;
}
