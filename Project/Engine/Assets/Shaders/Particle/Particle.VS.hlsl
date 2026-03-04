#include "Particle.hlsli"
    
StructuredBuffer<ParticleForGPU> gParticle : register(t0);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    output.texcoord = input.texcoord;
    output.position = mul(input.position, gParticle[instanceId].WVP);
    output.normal = normalize(mul(input.normal, (float32_t3x3) gParticle[instanceId].World));
    output.color = gParticle[instanceId].Color;
        return output;
}