cbuffer ShockwaveParams : register(b0) {
    float2 center; // 中心座標 (0-1)
    float time; // 時間経過
    float strength; // 強度
    float thickness; // 波の厚さ
    float speed; // 波の速度
    float2 padding; // パディング
}

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(VSOutput input) : SV_TARGET {
    float2 uv = input.texcoord;
    
    // 中心からの距離を計算
    float2 toCenter = uv - center;
    float distance = length(toCenter);
    
    // デフォルトのテクスチャカラー
    float4 color = gTexture.Sample(gSampler, uv);
    
    // 波の位置を計算
    float waveRadius = time * speed;
    
    // 波の影響を計算
    float waveDiff = abs(distance - waveRadius);
    float waveEffect = 1.0 - smoothstep(0.0, thickness, waveDiff);
    
    // 静的な歪みエフェクト（中心からの距離に基づく）
    float staticDistortion = strength * 0.1f * (1.0f - smoothstep(0.0f, 0.5f, distance));
    
    // 波による動的な歪み
    float dynamicDistortion = waveEffect * strength;
    
    // 総合的な歪み
    float totalDistortion = staticDistortion + dynamicDistortion;
    
    // 歪みを適用
    float2 distortion = normalize(toCenter) * totalDistortion;
    float2 distortedUV = uv + distortion;
    
    // 歪みが適用された場合のテクスチャをサンプリング
    if (totalDistortion > 0.001f) {
        color = gTexture.Sample(gSampler, distortedUV);
    }
    
    return color;
}