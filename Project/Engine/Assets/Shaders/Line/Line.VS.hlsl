// ライン描画用の頂点シェーダー
cbuffer Camera : register(b0)
{
    matrix viewProjection;
}

struct VSInput
{
    float3 position : POSITION;
    float3 color : COLOR;
    float alpha : ALPHA;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float alpha : ALPHA;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), viewProjection);
    output.color = input.color;
    output.alpha = input.alpha;
    return output;
}