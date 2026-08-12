// ColorLUTFill.CS.hlsl - .cube から読んだ LUT データを Texture3D へ書き込む
//
// CPU → Texture3D の直接アップロードは行の 256B アライン等の面倒が多い。
// アップロードヒープの StructuredBuffer を CS で 3D テクスチャへ写す方が
// 単純で、雲ノイズ生成（UAV 書き込みの Texture3D）と同じ流儀に揃う。
// LUT の差し替え時に 1 回だけ走る。

StructuredBuffer<float4> gLutData : register(t0); // .cube の並びそのまま（R が最内側）
RWTexture3D<float4> gLutTexture : register(u0);

cbuffer FillParams : register(b0)
{
    uint lutSize; // 一辺のサイズ N（データは N^3 要素）
    float3 pad;
};

static const uint kGroupSize = 4;

[numthreads(kGroupSize, kGroupSize, kGroupSize)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= lutSize || dispatchId.y >= lutSize || dispatchId.z >= lutSize)
    {
        return;
    }

    // .cube の並びは R が最内側 → index = x + y*N + z*N^2
    const uint index = dispatchId.x + dispatchId.y * lutSize + dispatchId.z * lutSize * lutSize;
    gLutTexture[dispatchId] = gLutData[index];
}
