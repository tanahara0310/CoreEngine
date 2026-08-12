// DoFComposite.CS.hlsl - シャープなフル解像度とボケたハーフ解像度の合成
//
// フル解像度で CoC を再計算し、|CoC| に応じてシャープ⇔ボケをブレンドする。
// ハーフ解像度の再計算にしないのは、焦点面上のディテールを 1px も失わないため。

Texture2D<float4> gTexture : register(t0); // フル解像度シーンカラー
Texture2D<float4> gBlurred : register(t1); // ギャザー結果（ハーフ解像度）
Texture2D<float> gDepth : register(t2);    // シーン深度
RWTexture2D<float4> gOutput : register(u0);

cbuffer DoFCompositeParams : register(b0)
{
    uint2 fullSize;
    uint2 halfSize;
    float focusDistance;
    float cocScalePx;
    float maxCocPx;
    float nearPlane;
    float farPlane;
    float3 pad;
};

float LinearDepth(float ndcDepth)
{
    return (nearPlane * farPlane) / (farPlane - ndcDepth * (farPlane - nearPlane));
}

float ComputeCoc(float viewDepth)
{
    const float coc = cocScalePx * (viewDepth - focusDistance) / max(viewDepth, 1e-3f);
    return clamp(coc, -maxCocPx, maxCocPx);
}

// 手動の双線形補間（サンプラー不使用の流儀）
float4 SampleBlurredBilinear(float2 uv)
{
    float2 texel = uv * (float2)halfSize - 0.5f;
    int2 base = (int2)floor(texel);
    float2 f = texel - (float2)base;

    const int2 maxCoord = int2((int)halfSize.x - 1, (int)halfSize.y - 1);
    float4 c00 = gBlurred.Load(int3(clamp(base + int2(0, 0), int2(0, 0), maxCoord), 0));
    float4 c10 = gBlurred.Load(int3(clamp(base + int2(1, 0), int2(0, 0), maxCoord), 0));
    float4 c01 = gBlurred.Load(int3(clamp(base + int2(0, 1), int2(0, 0), maxCoord), 0));
    float4 c11 = gBlurred.Load(int3(clamp(base + int2(1, 1), int2(0, 0), maxCoord), 0));

    return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
}

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= fullSize.x || dispatchId.y >= fullSize.y)
    {
        return;
    }

    const int2 coord = int2(dispatchId.xy);
    const float4 sharp = gTexture.Load(int3(coord, 0));

    const float2 uv = (float2(coord) + 0.5f) / float2(fullSize);
    const float4 blurred = SampleBlurredBilinear(uv);

    // 自分の CoC と、ボケ層が持ち込む CoC（前ボケの滲み出し）の大きい方でブレンドする。
    // 自分だけを見ると「手前のボケた物体の輪郭の外側」が急にシャープへ戻り、切り抜きに見える
    const float ownCoc = abs(ComputeCoc(LinearDepth(gDepth.Load(int3(coord, 0)))));
    const float neighborCoc = blurred.a * 2.0f; // ハーフ解像度px → フル解像度px
    const float coc = max(ownCoc, neighborCoc);

    // |CoC| 0.5px 以下は完全にシャープ、3px 以上で完全にボケ層
    const float blend = smoothstep(0.5f, 3.0f, coc);

    gOutput[coord] = float4(lerp(sharp.rgb, blurred.rgb, blend), sharp.a);
}
