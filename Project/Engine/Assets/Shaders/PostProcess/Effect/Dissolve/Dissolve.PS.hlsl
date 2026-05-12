// Dissolve Pixel Shader
// ディゾルブパラメータ
cbuffer DissolveParams : register(b0) {
    float threshold;
    float edgeWidth;
    float edgeColorR;
    float edgeColorG;
    float edgeColorB;
    float3 padding;
}

Texture2D<float4> inputTexture : register(t0);
Texture2D<float4> noiseTexture : register(t1);
SamplerState textureSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

float4 main(PSInput input) : SV_TARGET {
    float4 baseColor = inputTexture.Sample(textureSampler, input.texcoord);
    float noiseValue = noiseTexture.Sample(textureSampler, input.texcoord).r;
    
    // ディゾルブ計算（ノイズ値がthresholdより小さい場合は破棄）
    if (noiseValue < threshold) {
        discard;
    }
    
    // エッジの計算
    float edgeFactor = smoothstep(threshold, threshold + edgeWidth, noiseValue);
    
    // エッジカラーの適用
    float3 edgeColor = float3(edgeColorR, edgeColorG, edgeColorB);
    float3 finalColor = lerp(edgeColor, baseColor.rgb, edgeFactor);
    
    return float4(finalColor, baseColor.a);
}
