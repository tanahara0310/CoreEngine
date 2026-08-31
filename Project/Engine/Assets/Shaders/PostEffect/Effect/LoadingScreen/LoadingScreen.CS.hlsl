// LoadingScreen.CS.hlsl - ローディング画面 コンピュートシェーダー

#include "ShaderMath.hlsli" // PI / TWO_PI / HALF_PI

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer LoadingParams : register(b0)
{
    float screenAlpha;  // 表示強度 (0.0 = 非表示, 1.0 = 完全表示)
    float time;         // 経過時間
    float speed;        // 回転の速さ
    float spring;       // 行き過ぎの強さ

    float stepTurn;     // 1 段あたりの回転量（PI 単位）
    float arcCount;     // 弧の本数
    float arcLength;    // 弧の開き角（PI 単位）
    float arcPulse;     // 開き角の伸び縮み量

    float thickness;    // 弧の太さ（基準解像度でのピクセル）
    float radius;       // 弧の半径（基準解像度でのピクセル）
    float tipDot;       // 先端の玉 (0/1)
    float trackEnabled; // 軌道リング (0/1)

    float hueCycle;     // 色相の巡回 (0/1)
    float centerX;      // 表示位置（画面幅に対する比率）
    float centerY;      // 表示位置（画面高さに対する比率）
    float trackAlpha;   // 軌道リングの濃さ

    float progress;     // 読み込みの進捗 (0.0〜1.0)
    float gaugeAlpha;   // 進捗ゲージの表示強度
    float gaugePad0;
    float gaugePad1;

    float4 arcColor0;
    float4 arcColor1;
    float4 arcColor2;
    float4 trackColor;
};

cbuffer ScreenParams : register(b1)
{
    uint screenWidth;
    uint screenHeight;
    float2 pad;
};

static const uint  kGroupSize = 8;
static const float kReferenceHeight = 720.0f; // radius / thickness の基準解像度
static const float kEdgeWidth = 1.5f;         // 輪郭のぼかし幅（ピクセル）
static const float kTipScale = 1.24f;         // 先端の玉の半径 / 弧の太さの半分
static const int   kMaxArcCount = 4;
static const float kGaugeOffset = 2.4f;      // ゲージ半径 = 弧の半径 + 太さの半分 * これ
static const float kGaugeWidthScale = 0.55f; // ゲージの太さ / 弧の太さ

// 最終出力は sRGB へエンコードされるので、指定色はリニアへ戻してから合成する
float3 SrgbToLinear(float3 c)
{
    float3 lo = c / 12.92f;
    float3 hi = pow(max(c + 0.055f, 0.0f) / 1.055f, 2.4f);
    return lerp(lo, hi, step(0.04045f, c));
}

float3 HsvToRgb(float3 hsv)
{
    float4 k = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
    float3 p = abs(frac(hsv.xxx + k.xyz) * 6.0f - k.www);
    return hsv.z * lerp(k.xxx, saturate(p - k.xxx), hsv.y);
}

// 1.0 を通り過ぎてから戻るイージング。k が行き過ぎの強さ
float EaseOutBack(float t, float k)
{
    float u = t - 1.0f;
    return 1.0f + (k + 1.0f) * u * u * u + k * u * u;
}

float2 Rotate(float2 p, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}

// 丸端の円弧までの距離。sc は半開き角の (sin, cos)、ra は半径、rb は太さの半分
float SdArc(float2 p, float2 sc, float ra, float rb)
{
    p.x = abs(p.x);
    return ((sc.y * p.x > sc.x * p.y) ? length(p - sc * ra) : abs(length(p) - ra)) - rb;
}

float3 ArcColor(int index, float phase)
{
    if (hueCycle > 0.5f)
    {
        return SrgbToLinear(HsvToRgb(float3(frac(phase * 0.111f + (float)index * 0.39f), 0.65f, 0.95f)));
    }
    if (index == 0)
    {
        return SrgbToLinear(arcColor0.rgb);
    }
    if (index == 1)
    {
        return SrgbToLinear(arcColor1.rgb);
    }
    return SrgbToLinear(arcColor2.rgb);
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    float3 color = gTexture.Load(int3(dispatchId.xy, 0)).rgb;

    float uiScale = (float)screenHeight / kReferenceHeight;
    float ra = radius * uiScale;
    float rb = max(thickness * 0.5f * uiScale, 0.5f);
    float tipRadius = rb * kTipScale;
    float gaugeRadius = ra + rb * kGaugeOffset;
    float gaugeWidth = rb * kGaugeWidthScale;
    float bound = max(ra + max(rb, tipRadius), gaugeRadius + gaugeWidth) + kEdgeWidth * 2.0f;

    float2 center = float2(centerX * (float)screenWidth, centerY * (float)screenHeight);
    float2 p = (float2)dispatchId.xy + 0.5f - center;

    if (screenAlpha > 0.001f && dot(p, p) <= bound * bound)
    {
        if (trackEnabled > 0.5f)
        {
            float d = abs(length(p) - ra) - rb;
            float a = (1.0f - smoothstep(-kEdgeWidth, kEdgeWidth, d)) * trackAlpha * screenAlpha;
            color = lerp(color, SrgbToLinear(trackColor.rgb), a);
        }

        float phase = time * speed;
        float rotation = (floor(phase) + EaseOutBack(frac(phase), spring)) * PI * stepTurn;
        float span = clamp(arcLength + arcPulse * sin(phase * 3.2f), 0.02f, 1.98f) * PI;
        float halfAngle = span * 0.5f;
        float2 sc = float2(sin(halfAngle), cos(halfAngle));

        int count = clamp((int)arcCount, 1, kMaxArcCount);

        [loop]
        for (int i = 0; i < count; ++i)
        {
            float startAngle = rotation + TWO_PI * (float)i / (float)count;

            // 弧の中心方向を +Y 軸へ合わせてから SdArc を評価する
            float2 q = Rotate(p, HALF_PI - (startAngle + halfAngle));
            float d = SdArc(q, sc, ra, rb);

            if (tipDot > 0.5f)
            {
                float endAngle = startAngle + span;
                float2 tipPos = float2(cos(endAngle), sin(endAngle)) * ra;
                d = min(d, length(p - tipPos) - tipRadius);
            }

            float a = (1.0f - smoothstep(-kEdgeWidth, kEdgeWidth, d)) * screenAlpha;
            color = lerp(color, ArcColor(i, phase), a);
        }

        if (gaugeAlpha > 0.001f)
        {
            // ゲージの下地（全周）
            float dBase = abs(length(p) - gaugeRadius) - gaugeWidth;
            float aBase = (1.0f - smoothstep(-kEdgeWidth, kEdgeWidth, dBase)) * gaugeAlpha;
            color = lerp(color, SrgbToLinear(trackColor.rgb), aBase);

            // 進捗ぶんの弧（12 時から時計回り）
            float fill = saturate(progress);
            if (fill > 0.001f)
            {
                float halfFill = fill * PI;
                float2 q = Rotate(p, HALF_PI - (halfFill - HALF_PI));
                float d = SdArc(q, float2(sin(halfFill), cos(halfFill)), gaugeRadius, gaugeWidth);
                float a = (1.0f - smoothstep(-kEdgeWidth, kEdgeWidth, d)) * gaugeAlpha;
                color = lerp(color, ArcColor(0, phase), a);
            }
        }
    }

    gOutput[dispatchId.xy] = float4(color, 1.0f);
}
