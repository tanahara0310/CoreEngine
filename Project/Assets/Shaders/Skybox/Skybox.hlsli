struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float3 texcoord : TEXCOORD0;
};

struct TransformationMatrix
{
    float4x4 WVP;
};
