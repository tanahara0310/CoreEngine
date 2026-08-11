// BloomUpsample.CS.hlsl - ブルームのアップサンプル＋加算
//
// 1 段下（半分の解像度）を 3x3 テントフィルタで拡大し、同解像度のダウンサンプル結果へ加算する。
// 出力は入力とは別のリソースにすること（同じリソースを SRV と UAV に同時に割り当てられないため、
// 上り用のチェーンを別に確保している）。

Texture2D<float4>   gLower  : register(t0); // 1 段下（出力の半分の解像度）
Texture2D<float4>   gSame   : register(t1); // 出力と同解像度のダウンサンプル結果
RWTexture2D<float4> gOutput : register(u0);

cbuffer BloomUpsampleParams : register(b0)
{
    uint2 outputSize; // 出力解像度
    uint2 lowerSize;  // gLower の解像度
};

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= outputSize.x || dispatchId.y >= outputSize.y)
    {
        return;
    }

    const int2 maxLower = int2((int)lowerSize.x - 1, (int)lowerSize.y - 1);
    const int2 lowerBase = (int2)dispatchId.xy / 2;

    // 3x3 テント（1 2 1 / 2 4 2 / 1 2 1）/16。
    // 単純な最近傍だと拡大時にブロックが見えるので、必ず重み付けして広げる
    float3 sum = 0.0f;
    sum += gLower.Load(int3(clamp(lowerBase + int2(-1, -1), int2(0, 0), maxLower), 0)).rgb * 1.0f;
    sum += gLower.Load(int3(clamp(lowerBase + int2( 0, -1), int2(0, 0), maxLower), 0)).rgb * 2.0f;
    sum += gLower.Load(int3(clamp(lowerBase + int2( 1, -1), int2(0, 0), maxLower), 0)).rgb * 1.0f;
    sum += gLower.Load(int3(clamp(lowerBase + int2(-1,  0), int2(0, 0), maxLower), 0)).rgb * 2.0f;
    sum += gLower.Load(int3(clamp(lowerBase + int2( 0,  0), int2(0, 0), maxLower), 0)).rgb * 4.0f;
    sum += gLower.Load(int3(clamp(lowerBase + int2( 1,  0), int2(0, 0), maxLower), 0)).rgb * 2.0f;
    sum += gLower.Load(int3(clamp(lowerBase + int2(-1,  1), int2(0, 0), maxLower), 0)).rgb * 1.0f;
    sum += gLower.Load(int3(clamp(lowerBase + int2( 0,  1), int2(0, 0), maxLower), 0)).rgb * 2.0f;
    sum += gLower.Load(int3(clamp(lowerBase + int2( 1,  1), int2(0, 0), maxLower), 0)).rgb * 1.0f;
    float3 lower = sum * (1.0f / 16.0f);

    float3 same = gSame.Load(int3(dispatchId.xy, 0)).rgb;

    gOutput[dispatchId.xy] = float4(lower + same, 1.0f);
}
