// CAS（Contrast Adaptive Sharpening）ピクセルシェーダー
//
// AMD FidelityFX CAS 相当の適応的シャープ化。
// TAA は履歴を混ぜる以上どうしても解像感が落ちるため、その直後に置いて失った分を戻す。
//
// 「適応的」の意味:
//   単純なアンシャープマスクは、もともとコントラストの高い輪郭をさらに強調して
//   リンギング（縁の白/黒い縁取り）を作る。CAS は 3x3 近傍の min/max から
//   「そのピクセルがどれだけ余白を持っているか」を測り、
//   飽和しそうな場所ではシャープ量を自動的に下げる。
//
// HDR での扱い:
//   CAS の式は 0〜1 のレンジを前提にしている（min(mn, 2-mx) が余白の指標）。
//   ここはトーンマップ前の HDR なので、可逆トーンマップ x/(1+max(x)) で 0〜1 へ写して
//   シャープ化し、逆変換で戻す。こうしないと輝度 1 を超える領域でシャープが効かなくなる。

#include "FullScreen.hlsli"

Texture2D<float4> gSceneColor : register(t0); // TAA 解決後（TAA 無効時は SceneColor）

cbuffer CASParams : register(b0)
{
    float2 gScreenSize;
    float gSharpness; // 0 = 最弱 / 1 = 最強
    float gPad0_;
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

/// @brief 可逆トーンマップ（Reinhard 系）。HDR を 0〜1 へ写す
float3 TonemapForward(float3 c)
{
    return c / (1.0f + max(c.r, max(c.g, c.b)));
}

/// @brief TonemapForward の逆変換
float3 TonemapInverse(float3 c)
{
    const float m = max(c.r, max(c.g, c.b));
    return c / max(1.0f - m, 1e-5f);
}

/// @brief 範囲内へクランプしたうえで 1 テクセル読む
float3 LoadTonemapped(int2 coord, int2 screenMax)
{
    const int2 c = clamp(coord, int2(0, 0), screenMax);
    return TonemapForward(max(gSceneColor.Load(int3(c, 0)).rgb, 0.0f));
}

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;

    const int2 coord = int2(input.position.xy);
    const int2 screenMax = int2(gScreenSize) - int2(1, 1);

    //  a b c
    //  d e f
    //  g h i
    const float3 a = LoadTonemapped(coord + int2(-1, -1), screenMax);
    const float3 b = LoadTonemapped(coord + int2( 0, -1), screenMax);
    const float3 c = LoadTonemapped(coord + int2( 1, -1), screenMax);
    const float3 d = LoadTonemapped(coord + int2(-1,  0), screenMax);
    const float3 e = LoadTonemapped(coord, screenMax);
    const float3 f = LoadTonemapped(coord + int2( 1,  0), screenMax);
    const float3 g = LoadTonemapped(coord + int2(-1,  1), screenMax);
    const float3 h = LoadTonemapped(coord + int2( 0,  1), screenMax);
    const float3 i = LoadTonemapped(coord + int2( 1,  1), screenMax);

    // 十字（b/d/e/f/h）の min/max に、対角を含む 3x3 の min/max を足し込む。
    // 2 段構えにすることで、細い線の上（対角だけが外れ値）とベタ面を区別できる。
    float3 minRGB = min(min(min(d, e), min(f, b)), h);
    minRGB += min(min(min(a, c), min(g, i)), minRGB);

    float3 maxRGB = max(max(max(d, e), max(f, b)), h);
    maxRGB += max(max(max(a, c), max(g, i)), maxRGB);

    // 上下どちらに余白が少ないかで振幅を決める（min/max は上で 2 倍相当なので 2.0 と比較）
    const float3 amplitude = sqrt(saturate(min(minRGB, 2.0f - maxRGB) / max(maxRGB, 1e-5f)));

    // シャープ強度。FidelityFX と同じく -1/8（弱）〜 -1/5（強）の範囲を取る
    const float peak = -1.0f / lerp(8.0f, 5.0f, saturate(gSharpness));
    const float3 weight = amplitude * peak;

    // 十字の重み付き平均。weight が負なので中心が持ち上がり、周囲が引かれる＝シャープ化
    const float3 sharpened = ((b + d + f + h) * weight + e) / (1.0f + 4.0f * weight);

    output.color = float4(TonemapInverse(max(sharpened, 0.0f)), 1.0f);
    return output;
}
