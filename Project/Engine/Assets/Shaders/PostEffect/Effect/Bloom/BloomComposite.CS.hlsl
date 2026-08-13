// BloomComposite.CS.hlsl - シーンへブルームを加算合成する最終段
//
// gBloom はフル解像度の 1/2。線形補間で拡大してから加算する。

Texture2D<float4>   gScene  : register(t0); // フル解像度のシーン
Texture2D<float4>   gBloom  : register(t1); // 1/2 解像度のブルーム
Texture2D<float>    gDirt   : register(t2); // レンズダートマスク（正方形・手続き生成）
RWTexture2D<float4> gOutput : register(u0);

cbuffer BloomCompositeParams : register(b0)
{
    uint2 outputSize;    // フル解像度
    uint2 bloomSize;     // gBloom の解像度
    float intensity;     // ブルーム強度
    float dirtIntensity; // ダートの寄与倍率（0 で無効）
    uint  dirtSize;      // gDirt の一辺
    float padding;
};

static const uint kGroupSize = 8;

// 手動の双線形補間（このエンジンのポストエフェクトはサンプラーを持たないため）
float3 SampleBloomBilinear(float2 uv)
{
    float2 texel = uv * (float2)bloomSize - 0.5f;
    int2   base  = (int2)floor(texel);
    float2 frac0 = texel - (float2)base;

    const int2 maxCoord = int2((int)bloomSize.x - 1, (int)bloomSize.y - 1);
    float3 c00 = gBloom.Load(int3(clamp(base + int2(0, 0), int2(0, 0), maxCoord), 0)).rgb;
    float3 c10 = gBloom.Load(int3(clamp(base + int2(1, 0), int2(0, 0), maxCoord), 0)).rgb;
    float3 c01 = gBloom.Load(int3(clamp(base + int2(0, 1), int2(0, 0), maxCoord), 0)).rgb;
    float3 c11 = gBloom.Load(int3(clamp(base + int2(1, 1), int2(0, 0), maxCoord), 0)).rgb;

    return lerp(lerp(c00, c10, frac0.x), lerp(c01, c11, frac0.x), frac0.y);
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= outputSize.x || dispatchId.y >= outputSize.y)
    {
        return;
    }

    float4 scene = gScene.Load(int3(dispatchId.xy, 0));
    float2 uv = ((float2)dispatchId.xy + 0.5f) / (float2)outputSize;
    float3 bloom = SampleBloomBilinear(uv);

    // レンズダート: レンズの汚れがブルーム光を散乱して光る。
    // ブルーム自体に乗算するので、光源が無い場所では汚れは見えない（実カメラと同じ）
    float dirt = 0.0f;
    if (dirtIntensity > 0.0f)
    {
        const uint2 dirtCoord = min(uint2(uv * float(dirtSize)), dirtSize - 1);
        dirt = gDirt.Load(int3(dirtCoord, 0));
    }

    gOutput[dispatchId.xy] = float4(scene.rgb + bloom * (intensity + dirt * dirtIntensity), scene.a);
}
