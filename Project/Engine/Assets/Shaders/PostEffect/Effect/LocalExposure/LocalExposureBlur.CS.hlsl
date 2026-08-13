// LocalExposureBlur.CS.hlsl - ベース層の分離ガウシアンブラー（H/V 共用）
//
// ローカル露出のベース層は「画面の 1/4 程度に広がる低周波」でなければならない。
// 狭いと模様のコントラストまで潰れて HDR 写真のような眠い絵になる。
// 1/8 解像度でのガウシアン半径 12（σ=6）はフル解像度で約 100px の広がりに相当する。

Texture2D<float> gSource : register(t0);
RWTexture2D<float> gOutput : register(u0);

cbuffer BlurParams : register(b0)
{
    uint2 textureSize;
    uint2 direction; // (1,0)=水平 / (0,1)=垂直
};

static const uint kGroupSize = 8;
static const int kRadius = 12;
static const float kSigma = 6.0f;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= textureSize.x || dispatchId.y >= textureSize.y)
    {
        return;
    }

    const int2 coord = int2(dispatchId.xy);
    const int2 dir = int2(direction);

    float sum = 0.0f;
    float weightSum = 0.0f;
    for (int i = -kRadius; i <= kRadius; ++i)
    {
        const int2 p = clamp(coord + dir * i, int2(0, 0), int2(textureSize) - 1);
        const float w = exp(-0.5f * (float(i) * float(i)) / (kSigma * kSigma));
        sum += gSource.Load(int3(p, 0)) * w;
        weightSum += w;
    }

    gOutput[coord] = sum / weightSum;
}
