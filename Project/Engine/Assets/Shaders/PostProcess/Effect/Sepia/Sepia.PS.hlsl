// テクスチャとサンプラー
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

// セピアパラメータ用の定数バッファ
cbuffer SepiaParams : register(b0)
{
    float intensity;      // セピア効果強度
    float toneRed;        // 赤色調整
    float toneGreen;      // 緑色調整
    float toneBlue;       // 青色調整
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PSOutput
{
    float4 color : SV_TARGET0;
};

PSOutput main(PSInput input)
{
    PSOutput output;

    // 入力テクスチャカラー取得
    output.color = gTexture.Sample(gSampler, input.texcoord);

    // 元の色をグレースケールに変換（輝度計算）
    float luminance = dot(output.color.rgb, float3(0.299f, 0.587f, 0.114f));

    // セピア色調の基準となる色（茶色系）
    float3 sepiaColor = float3(
        luminance * toneRed,   // 赤成分（温かみのある色調）
        luminance * toneGreen, // 緑成分（中間色調）
        luminance * toneBlue   // 青成分（冷たい色調を抑制）
    );

    // 元の色とセピア色を混合
    output.color.rgb = lerp(output.color.rgb, sepiaColor, intensity);

    return output;
}