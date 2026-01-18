struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
};

struct Material
{
    float4 color;
    float4x4 uvTransform;
};
