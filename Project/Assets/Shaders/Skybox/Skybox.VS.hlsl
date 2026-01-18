#include "Skybox.hlsli"

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    
    // 位置を変換（xywwのトリックで最遠方に配置）
    output.position = mul(input.position, gTransformationMatrix.WVP).xyww;
    
    // texcoordは頂点位置をそのまま出力（3次元ベクトル）
    output.texcoord = input.position.xyz;
    
    return output;
}
