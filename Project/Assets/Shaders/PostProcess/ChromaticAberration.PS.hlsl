// Improved Chromatic Aberration Post Effect Pixel Shader
// より効果的で高品質な色収差エフェクト

#include "FullScreen.hlsli"

// 入力テクスチャ
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// 定数バッファ
cbuffer ChromaticAberrationParams : register(b0)
{
    float intensity;   // 色収差の強度
    float radialFactor;           // 放射状の強度
    float centerX;        // 中心X座標
    float centerY;     // 中心Y座標
    
    float2 redOffset;     // 赤チャンネルのオフセット（互換性のため残すが使用しない）
    float2 greenOffset;  // 緑チャンネルのオフセット
    float2 blueOffset;         // 青チャンネルのオフセット
    
    float distortionScale;        // 歪み量のスケール
    float falloff;          // 端に向かう強度の減衰
    float samples; // サンプリング回数
    float padding;
};

// 入力構造体
struct PixelShaderInput
{
    float4 position : SV_POSITION;
 float2 texcoord : TEXCOORD0;
};

// 出力構造体
struct PixelShaderOutput
{
    float4 color : SV_Target;
};

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;
    
    float2 uv = input.texcoord;
    float2 center = float2(centerX, centerY);
    
    // 中心からの相対位置ベクトルを計算
    float2 direction = uv - center;
    float distance = length(direction);
    
    // 方向を正規化（ゼロ除算を防ぐ）
    float2 normalizedDir = distance > 0.0001 ? direction / distance : float2(0.0, 0.0);
    
    // 中心からの距離に基づいて色収差の強度を計算
    // 距離が遠いほど強く、falloffで減衰の度合いを調整
    float radialIntensity = pow(saturate(distance * radialFactor), falloff);
    
    // 画面端に近いほど強くする追加効果
    float edgeDistance = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    float edgeFactor = 1.0 - smoothstep(0.0, 0.3, edgeDistance);
    
    // 最終的な色収差の強度
 float aberrationStrength = intensity * 0.001 * distortionScale * (radialIntensity + edgeFactor * 0.3);
    
    // 効果が非常に小さい場合はオリジナルをそのまま返す（最適化）
    if (aberrationStrength < 0.00001)
    {
        output.color = gTexture.Sample(gSampler, uv);
        return output;
    }

    // 色収差の計算：RGB各チャンネルを中心から放射状に異なる距離でサンプリング
    // 赤は外側（正の方向）、緑は中心、青は内側（負の方向）にずらす
    // これにより、レンズの色収差を正確にシミュレート
    
    // 赤チャンネル：中心から最も遠く（外側）
    float redDistance = aberrationStrength * 1.5;  // 赤が最も外側
    float2 redUV = uv + normalizedDir * redDistance;
    
    // 緑チャンネル：基準（ほぼずれなし）
    float greenDistance = aberrationStrength * 0.0;  // 緑は基準
    float2 greenUV = uv + normalizedDir * greenDistance;
    
    // 青チャンネル：中心に最も近く（内側）
    float blueDistance = aberrationStrength * -1.5;  // 青が最も内側
    float2 blueUV = uv + normalizedDir * blueDistance;
    
    // 各チャンネルをサンプリング（境界チェック付き）
    float3 color = float3(0.0, 0.0, 0.0);
    
    // 赤チャンネル
    if (all(redUV >= 0.0) && all(redUV <= 1.0))
        color.r = gTexture.Sample(gSampler, redUV).r;
    else
        color.r = gTexture.Sample(gSampler, uv).r; // 範囲外なら元の位置
    
    // 緑チャンネル（基準）
    if (all(greenUV >= 0.0) && all(greenUV <= 1.0))
        color.g = gTexture.Sample(gSampler, greenUV).g;
    else
   color.g = gTexture.Sample(gSampler, uv).g;
    
    // 青チャンネル
    if (all(blueUV >= 0.0) && all(blueUV <= 1.0))
        color.b = gTexture.Sample(gSampler, blueUV).b;
    else
        color.b = gTexture.Sample(gSampler, uv).b;
    
    // アルファチャンネルはオリジナルを使用
    float alpha = gTexture.Sample(gSampler, uv).a;
    
    output.color = float4(color, alpha);
    
    return output;
}