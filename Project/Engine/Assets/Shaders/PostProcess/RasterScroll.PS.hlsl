#include "FullScreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// ラスタースクロールパラメータ用の定数バッファ
cbuffer RasterScrollParams : register(b0)
{
    float scrollSpeed;          // スクロール速度
    float lineHeight;           // ライン高さ
    float amplitude;            // 振幅
    float frequency;            // 周波数
    
    float time;                 // 時間
    float lineOffset;           // ライン開始位置オフセット
    float distortionStrength;   // 歪み強度
    float padding;              // パディング
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PixelShaderOutput
{
    float4 color : SV_Target;
};

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;
    
    float2 uv = input.texcoord;
    
    // 滑らかな波状歪みの計算（連続的な関数を使用）
    // Y座標に基づく基本的な水平波
    float wavePhase = (uv.y + lineOffset) * frequency * 6.28318530718; // 2π * frequency
    float horizontalWave = sin(wavePhase + time * scrollSpeed) * amplitude;
    
    // より細かい波の重ね合わせ（複数の周波数で豊かな表現）
    float detailWave1 = sin(uv.y * lineHeight * 2.0 + time * scrollSpeed * 1.5) * amplitude * 0.3;
    float detailWave2 = sin(uv.y * lineHeight * 0.5 + time * scrollSpeed * 0.7) * amplitude * 0.5;
    
    // X座標に基づく縦方向の微細な歪み
    float verticalWave = sin(uv.x * frequency * 4.0 + time * scrollSpeed * 0.8) * amplitude * 0.15;
    
    // 対角線方向の歪み（より自然な歪み効果）
    float diagonalWave = sin((uv.x + uv.y) * frequency * 2.0 + time * scrollSpeed * 1.2) * amplitude * 0.2;
    
    // 全ての波を合成
    float2 totalDistortion = float2(
        horizontalWave + detailWave1 + detailWave2 + diagonalWave * 0.5,
        verticalWave + diagonalWave * 0.3
    ) * distortionStrength;
    
    // UV座標を歪ませる
    float2 distortedUV = uv + totalDistortion;
    
    // 境界処理：サンプリング座標を適切に処理（ラップアラウンドではなくクランプ）
    // これにより境界での不連続を防ぐ
    distortedUV = saturate(distortedUV);
    
    // エッジでのフェードアウト効果（境界での不自然さを軽減）
    float2 edgeMask = smoothstep(0.0, 0.05, distortedUV) * smoothstep(1.0, 0.95, distortedUV);
    float edgeFactor = edgeMask.x * edgeMask.y;
    
    // 歪んだ座標と元の座標をエッジファクターで補間
    float2 finalUV = lerp(uv, distortedUV, edgeFactor);
    
    // テクスチャサンプリング
    output.color = gTexture.Sample(gSampler, finalUV);
    
    // 滑らかな走査線効果（連続的な関数を使用）
    float scanlinePhase = uv.y * lineHeight * 2.0 + time * scrollSpeed * 0.3;
    float scanlineEffect = 1.0 + 0.03 * sin(scanlinePhase) + 0.02 * sin(scanlinePhase * 2.7);
    
    // ソフトなコントラスト調整
    float contrastBoost = 1.0 + 0.05 * sin(uv.y * frequency + time * scrollSpeed * 0.5);
    
    output.color.rgb *= scanlineEffect * contrastBoost;
    
    // 歪みの強さに応じてわずかに色調を調整（より自然な見た目）
    float distortionIntensity = length(totalDistortion);
    float colorShift = sin(distortionIntensity * 10.0 + time * scrollSpeed) * 0.02;
    output.color.rgb += float3(colorShift, -colorShift * 0.5, colorShift * 0.3);
    
    return output;
}