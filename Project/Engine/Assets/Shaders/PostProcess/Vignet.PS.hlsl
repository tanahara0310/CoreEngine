// テクスチャとサンプラー
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

// ヴィネットパラメータ用の定数バッファ
cbuffer VignetteParams : register(b0)
{
    float intensity;      // ヴィネット強度
    float smoothness;     // 滑らかさ
    float size;           // サイズ
    float padding;        // パディング
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PSOutput
{
    float4 color : SV_TARGET;
};

PSOutput main(PSInput input)
{
    PSOutput output;

    // 入力テクスチャカラー取得
    output.color = gTexture.Sample(gSampler, input.texcoord);

    // 周囲ほどに、中心ほど明るくなるように計算で調整
    float2 correct = input.texcoord * (1.0f - input.texcoord.yx);

    // correctだけで計算すると中心の最大値が0.0625で暗すぎるので調整
    // この例では指定されたサイズ倍して最大値を1にしている
    float vignette = correct.x * correct.y * size;

    // 滑らかさパラメータを適用
    vignette = saturate(pow(vignette, smoothness));

    // 強度パラメータを適用
    vignette = lerp(0.0f, vignette, intensity);

    // 係数として乗算
    output.color.rgb *= vignette;

    return output;
}