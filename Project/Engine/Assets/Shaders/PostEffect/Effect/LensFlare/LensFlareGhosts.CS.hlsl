// LensFlareGhosts.CS.hlsl - ゴースト + ハロー生成
// ゴースト: 実カメラのゴーストは「絞り開口の像」なので、点サンプルの滲みではなく
// 光源色で中を均一に塗り潰した多角形/円形（絞り羽根形状）として描く。
// 位置は画面中心対称のゴースト列（レンズ内反射の直線配列）を解析的に求める。
// ハロー: 輝度抽出バッファの反転サンプル（John Chapman 方式）による円環。
//
// gSourcePos は LensFlareFindSource.CS.hlsl が輝度抽出バッファから検出した
// 支配的な光源位置。ゴーストの中心位置とその塗り潰し色の取得に使う。

#include "LensFlareCommon.hlsli"

Texture2D<float4> gBright : register(t0);
Texture2D<float2> gSourcePos : register(t1);
RWTexture2D<float4> gFeatures : register(u0);
SamplerState gLinearClamp : register(s0);

static const uint kGroupSize = 8;

// 中心方向 direction に沿って RGB をずらしてサンプル（色収差）
float3 SampleChromatic(float2 uv, float2 offset)
{
    const float r = gBright.SampleLevel(gLinearClamp, uv - offset, 0).r;
    const float g = gBright.SampleLevel(gLinearClamp, uv, 0).g;
    const float b = gBright.SampleLevel(gLinearClamp, uv + offset, 0).b;
    return float3(r, g, b);
}

// ゴースト毎のティント。コサインパレットで暖色→寒色の色変化を作る。
// 彩度は低く抑えること: 実写のゴーストの色はレンズコーティングの反射色由来の
// ごく淡い色付きで、0.55±0.45 のような高彩度だとピンク/緑のパステルシールに
// 見えて嘘っぽくなる（ユーザー報告の違和感の一因）。
float3 GhostTint(uint i, uint count)
{
    const float t = (count > 1) ? (float)i / (float)(count - 1) : 0.0f;
    return 0.78f + 0.22f * cos(TWO_PI * (t * 0.7f + float3(0.0f, 0.33f, 0.67f)));
}

// RGB シフト量をアスペクト比に依らず等方（画面上で真円状）にする。
// texel を軸ごとにそのまま使うと非正方形バッファで x/y のシフト量が異なり、
// 高次ゴーストほど楕円状に間延びしたにじみになる（実機で確認した不具合）。
float2 ComputeChromaOffset(float2 toCenter, float aspect, float2 flareSize, float magnitude)
{
    float2 dirAsp = LensFlareToAspect(toCenter, aspect);
    dirAsp = normalize(dirAsp + 1e-5f);
    const float uniformTexel = 1.0f / flareSize.y;
    float2 offsetAsp = dirAsp * uniformTexel * magnitude;
    return float2(offsetAsp.x / aspect, offsetAsp.y);
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= gFlareWidth || dispatchId.y >= gFlareHeight)
    {
        return;
    }

    const float2 flareSize = float2(gFlareWidth, gFlareHeight);
    const float aspect = flareSize.x / flareSize.y;

    const float2 uv = (float2(dispatchId.xy) + 0.5f) / flareSize;
    // 画面中心で反転した UV（レンズ内反射でゴーストは光源の点対称側に出る）
    const float2 flippedUv = 1.0f - uv;

    // 中心へ向かうベクトル（ゴースト列はこの直線上に並ぶ。実レンズでも光源と
    // 光軸＝画面中心を結ぶ直線上にゴーストが整列するため、screen center を
    // 基準にした反射・分散の位置決め自体は光源位置を知らなくても物理的に正しい）
    const float2 toCenter = uv - 0.5f;
    const float2 ghostVec = toCenter * gGhostDispersal;

    const float2 sourceUv = gSourcePos.Load(int3(0, 0, 0));

    // ===== ゴーストの塗り潰し色（光源のコア色） =====
    // 光源位置の周辺を数タップ平均して輝度抽出バッファから光源色を得る。
    // トーン正規化（/(1+lum)）で「色相・遮蔽率は保ちつつ HDR の絶対輝度に依らない」
    // 単位スケールの色にする。太陽の生輝度（数百）をそのまま塗ると ACES の飽和域で
    // 真っ白な多角形になり、絞り羽根の淡い色付きゴーストにならない。
    // 雲が太陽を隠すと bright バッファ自体が暗くなるため遮蔽の応答は保たれる。
    const float2 srcTexel = 1.0f / flareSize;
    float3 sourceColor = gBright.SampleLevel(gLinearClamp, sourceUv, 0).rgb;
    sourceColor += gBright.SampleLevel(gLinearClamp, sourceUv + float2( 2.0f,  0.0f) * srcTexel, 0).rgb;
    sourceColor += gBright.SampleLevel(gLinearClamp, sourceUv + float2(-2.0f,  0.0f) * srcTexel, 0).rgb;
    sourceColor += gBright.SampleLevel(gLinearClamp, sourceUv + float2( 0.0f,  2.0f) * srcTexel, 0).rgb;
    sourceColor += gBright.SampleLevel(gLinearClamp, sourceUv + float2( 0.0f, -2.0f) * srcTexel, 0).rgb;
    sourceColor *= 0.2f;
    sourceColor /= (1.0f + Luminance(sourceColor));

    float3 result = 0.0f;

    // ===== ゴースト列（絞り開口の像として塗り潰す） =====
    [loop]
    for (uint i = 0; i < gGhostCount; ++i)
    {
        // このゴーストの中心位置（出力 UV 空間）を逆算する。
        // sampleUv(uv) = flippedUv + ghostVec*i は uv に関するアフィン変換なので、
        // 「光源 sourceUv がどの出力ピクセルに現れるか」は解析的に逆算できる。
        const float s = gGhostDispersal * (float)i;
        const float denom = 1.0f - s;
        const float denomSafe = (abs(denom) < 1e-4f) ? ((denom >= 0.0f) ? 1e-4f : -1e-4f) : denom;
        const float2 ghostCenterUv = (1.0f - sourceUv - 0.5f * s) / denomSafe;

        const float2 d = LensFlareToAspect(uv - ghostCenterUv, aspect);

        // 絞り羽根形状（多角形）と、絞り面から外れたボケ（円形）をゴースト毎に混在させる。
        // 実レンズでも全てのゴーストが多角形になるわけではなく、両方が混在するのが写実的。
        // ブレンド（距離比の lerp）ではなく二択にすること: 中間値のブレンドは角が
        // 丸まって結局円に見え、多角形ゴーストが1つも出ない（実機で確認した不具合）。
        const float hashA = HashSine11((float)i * 17.13f + 4.0f);
        const float hashB = HashSine11((float)i * 3.7f + 1.0f);

        // レンズ内反射の像倍率: この配置変換（アフィン）は出力空間で 1/|1-s| 倍の
        // 拡大に相当する。ゴーストの大きさを倍率と連動させると「太陽側に弾かれた
        // ものは大きく淡い・中心へ収束するものは小さい」という実写の変化が出る。
        // ハッシュだけで大きさを決めると全ゴーストが同サイズの均一なシールに見える
        // （ユーザー報告の違和感の一因）。
        const float magnification = clamp(1.0f / max(abs(denom), 0.35f), 0.6f, 2.6f);
        const float radius = gGhostRadius * magnification * lerp(0.75f, 1.25f, hashB);

        // 拡大された像はエネルギーが面積に分散して暗くなる（∝(1-s)²だが完全準拠だと
        // 高次がほぼ消えるため一乗＋クランプで緩和）。加えて高次反射ほど減衰させる。
        const float energyFade = clamp(abs(denom), 0.4f, 1.0f);
        const float orderFade = 1.0f / (1.0f + 0.35f * (float)i);

        const bool usePolygon = (hashA < gGhostPolygonMix);
        // 絞りはレンズ鏡筒に固定されているため、全ゴーストの多角形は同じ向きになる
        // （ゴースト毎のランダム回転は実写と一致しない）
        const float rotation = gApertureRotationRad;

        // 色収差はエッジフリンジとして表現する: R/G/B でマスク半径を僅かに変えると
        // 縁だけに色ズレが出る。画像サンプルの RGB シフト方式（旧実装）は光源の
        // 放射状グラデーション全体が虹に分解されて不自然だった。
        const float fringe = gChromaDistortion * 0.02f * (1.0f + 0.5f * (float)i);
        float3 mask;
        float ratioG = 0.0f;
        [unroll]
        for (uint ch = 0; ch < 3; ++ch)
        {
            const float chScale = 1.0f + fringe * (1.0f - (float)ch); // R:+f, G:1, B:-f
            const float r = radius * chScale;
            const float ratio = usePolygon
                ? LensFlarePolygonDistanceRatio(d, r, gApertureBladeCount, rotation)
                : LensFlareCircleDistanceRatio(d, r);
            // 中を均一に埋めて縁だけ柔らかく落とす（絞りの像は中身の詰まった形）。
            // フェード幅は狭くすること: 正六角形は外接半径と内接半径の差が 13% しかなく、
            // これより広いフェード（例: 0.75〜1.0 = 25%）だと輪郭が塗り潰されて
            // 円にしか見えなくなる（オフライン検証で確認済み。数式は正しくても見えない）。
            mask[ch] = 1.0f - smoothstep(0.94f, 1.0f, ratio);
            if (ch == 1) { ratioG = ratio; }
        }

        // 内側をわずかに暗くして「ガラス面の像」らしい薄い立体感を出す
        const float interior = lerp(0.7f, 1.0f, saturate(ratioG));

        // ゴースト中心が画面外へ遠く外れたものはフェード（画面端で急に切れるのを防ぐ）
        const float centerDist = length(LensFlareToAspect(ghostCenterUv - 0.5f, aspect));
        const float centerFade = saturate(1.0f - (centerDist - 0.7f) / 0.5f);

        // 光源（太陽）に重なるゴーストは減光する。実写では直達光のベーリンググレアが
        // 支配的で、太陽の直上にくっきりした多角形が「乗って」見えることはない
        const float srcDist = length(LensFlareToAspect(ghostCenterUv - sourceUv, aspect));
        const float glareFade = lerp(0.35f, 1.0f, smoothstep(0.04f, 0.2f, srcDist));

        result += sourceColor * GhostTint(i, gGhostCount) * mask
            * (interior * centerFade * energyFade * orderFade * glareFade);
    }
    result *= gGhostIntensity;

    // ===== ハロー（アスペクト補正した円環） =====
    // アスペクト補正空間で中心方向へ一定距離ずらすと、光源が中心から
    // gHaloWidth の円環上に引き伸ばされる。ハローは絞り羽根の影響を受けにくい
    // （アパーチャそのものではなく、レンズ表面のコーティング反射で生じるため）
    // ので円形のまま据え置く。
    float2 haloVec = toCenter;
    haloVec.x *= aspect;
    haloVec = normalize(haloVec + 1e-5f);
    haloVec.x /= aspect;
    haloVec *= gHaloWidth;

    const float2 haloUv = flippedUv + haloVec;
    float haloWeight = length(0.5f - haloUv) / 0.7071f;
    haloWeight = pow(saturate(1.0f - haloWeight), 12.0f);

    // 色収差は控えめに（大きくすると光源の放射状グラデーション全体が虹に分解され、
    // ハローが虹のアーチになってしまう。実レンズのハローは縁が僅かに色付く程度）
    const float2 haloChromaOffset = ComputeChromaOffset(toCenter, aspect, flareSize, gChromaDistortion * 0.7f);
    result += SampleChromatic(haloUv, haloChromaOffset) * gHaloIntensity * haloWeight;

    gFeatures[dispatchId.xy] = float4(result, 1.0f);
}
