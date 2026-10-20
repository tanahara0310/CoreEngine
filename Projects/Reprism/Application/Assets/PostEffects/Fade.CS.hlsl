#include "PostEffectMaterial.hlsli"

// 画面のフェード。fadeType 0 = 黒へ ／ 1 = 白へ ／ 9 = ブロックに割れて落ちる
// 進み具合は progress × completeFactor（ブロックが落ちきるまでは 1 を超えて進む）。
// シーンの遷移が progress を毎フレーム入れる。遷移の外ではスクリプトが入れる
struct Params
{
    float progress;       // 0 = 素通し ／ 1 = 進みきり
    float transitionIn;   // 明ける段なら 1（遷移が入れる）
    float completeFactor; // progress 1 のときの進み具合
    float fadeType;       // 0 = 黒 ／ 1 = 白 ／ 9 = ブロック落下
    float timeOrigin;     // 揺れの時間の起点（gPostEffect.time で測る）
    float pixelBlockSize; // ブロックの大きさ（画面の UV）
    float fallSpeed;      // 落ちる速さの倍率
};
ConstantBuffer<Params> gMaterial : register(b0);

float3 PixelFallDown(float2 uv, float alpha, float blockSize, float fallSpeed, float t, out float outMask)
{
    float2 blockCoord = floor(uv / blockSize);

    // ブロックごとの値（2 系統）
    float2 q         = frac(blockCoord * float2(127.1f, 311.7f));
    q               += dot(q, q.yx + 19.19f);
    float blockHash  = frac((q.x + q.y) * q.x);
    float blockHashX = frac(blockHash * 17.39f);

    // 落ち始める時刻はブロックごとにばらつく
    float startTime = blockHash * 0.85f;
    float tFall     = max(0.0f, alpha - startTime);

    // 重力で加速して落ちる
    float gravY = tFall * tFall * fallSpeed * 5.0f;

    // 落ち始めてから横へ散る
    float xDrift = (blockHashX - 0.5f) * 0.07f * saturate(tFall * 4.0f);

    // 落ちる直前の小さな揺れ
    float preVibAmt = saturate(1.0f - (startTime - alpha) * 14.0f) * (1.0f - saturate(tFall * 10.0f));
    float vibOffset = sin(t * 60.0f + blockHash * 6.28318f) * 0.003f * preVibAmt;

    float2 sampleUV = clamp(uv + float2(xDrift + vibOffset, gravY), 0.0f, 1.0f);

    // 落ち始めたら色を横へずらす
    float caAmt = saturate(tFall * 5.0f) * 0.012f;
    float r = SampleScreen(clamp(sampleUV + float2(caAmt, 0.0f), 0.0f, 1.0f)).r;
    float g = SampleScreen(sampleUV).g;
    float b = SampleScreen(clamp(sampleUV - float2(caAmt, 0.0f), 0.0f, 1.0f)).b;
    float3 col = float3(r, g, b);

    // 画面の外まで落ちたら黒
    outMask = (sampleUV.y >= 0.999f || gravY > 1.8f) ? 1.0f : 0.0f;

    float2 blockFrac = frac(uv / blockSize);
    float2 edgeDist  = min(blockFrac, 1.0f - blockFrac);
    float  minEdge   = min(edgeDist.x, edgeDist.y);
    float borderWidth = 0.06f;

    // 落ちる前は境目に暗い溝
    float preBorder = saturate(1.0f - minEdge / borderWidth);
    col *= lerp(1.0f, 0.25f, preBorder * (1.0f - saturate(tFall * 8.0f)));

    // 落ち始めは境目が白く光る
    float glowFade = saturate(tFall * 12.0f) * saturate(1.0f - tFall * 3.0f);
    col += float3(1.0f, 1.0f, 1.0f) * saturate(1.0f - minEdge / (borderWidth * 1.5f)) * glowFade * 3.0f;

    // 落ちている間は下の端に琥珀色の残光
    float trailGlow = saturate(1.0f - blockFrac.y / 0.15f);
    float trailFade = saturate(tFall * 8.0f) * saturate(1.0f - tFall * 2.0f);
    col += float3(1.0f, 0.75f, 0.2f) * trailGlow * trailFade * 2.5f;

    return col;
}

POST_EFFECT_THREADS
void main(uint3 id : SV_DispatchThreadID)
{
    if (IsOutsideScreen(id.xy)) {
        return;
    }

    float2 uv = PostEffectUV(id.xy);
    float3 originalColor = SampleScreen(uv).rgb;
    float fadeAlpha = max(gMaterial.progress * gMaterial.completeFactor, 0.0f);

    if (gMaterial.fadeType > 8.5f && gMaterial.fadeType < 9.5f)
    {
        // 進むにつれて色が抜ける
        float luma       = dot(originalColor, float3(0.299f, 0.587f, 0.114f));
        float desatT     = saturate(fadeAlpha * 1.6f);

        // 進むにつれて画面の端が暗くなる
        float2 vigUV    = uv * (1.0f - uv);
        float  vignette = saturate(pow(vigUV.x * vigUV.y * 16.0f, 0.3f));
        float  vigStr   = saturate(fadeAlpha * 3.0f);

        float fallMask;
        float time = gPostEffect.time - gMaterial.timeOrigin;
        float3 fallenColor = PixelFallDown(uv, fadeAlpha,
                                           max(gMaterial.pixelBlockSize, 0.005f),
                                           gMaterial.fallSpeed, time, fallMask);

        float lumaF       = dot(fallenColor, float3(0.299f, 0.587f, 0.114f));
        float3 finalColor = lerp(fallenColor, float3(lumaF, lumaF, lumaF), desatT * 0.5f);
        finalColor       *= lerp(1.0f, vignette, vigStr);
        finalColor        = lerp(finalColor, float3(0.0f, 0.0f, 0.0f), fallMask);

        gOutput[id.xy] = float4(finalColor, 1.0f);
        return;
    }

    if (gMaterial.fadeType > 0.5f && gMaterial.fadeType < 1.5f)
    {
        gOutput[id.xy] = float4(lerp(originalColor, float3(1.0f, 1.0f, 1.0f), fadeAlpha), 1.0f);
        return;
    }

    gOutput[id.xy] = float4(lerp(originalColor, float3(0.0f, 0.0f, 0.0f), fadeAlpha), 1.0f);
}
