#include "Object3dVertex.hlsli"

VertexShaderOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    return VertexMain(input, instanceID);
}
