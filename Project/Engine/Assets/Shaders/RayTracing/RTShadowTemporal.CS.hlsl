// ============================================================
// RT シャドウ テンポラル蓄積（SVGF 風）
//   RayGen の生バイナリを 3×3 幾何ガイドで前処理し、
//   再投影 + Variance Clamping で履歴とブレンドする。
//   RayGen → このパス → A-Trous（表示用）の順。
//   history には A-Trous 出力ではなくこのパスの出力を保存する。
// ============================================================

// 入力: RayGen の生バイナリ出力（0=影, 1=光）
Texture2D<float> gRawShadow : register(t0);

// G-Buffer: 法線（空間前処理の幾何ガイド用）
Texture2D<float4> gGBufferNormal : register(t1);

// G-Buffer: ワールド座標（深度ガイド・背景判定用）
Texture2D<float4> gGBufferWorldPos : register(t2);

// 前フレームの Temporal 出力（この Temporal パスの結果を保存した履歴）
Texture2D<float> gHistoryShadow : register(t3);

// モーションベクター（NDC差分、GBuffer 産）
Texture2D<float2> gMotionVector : register(t4);

// 出力: テンポラル蓄積済みシャドウ
RWTexture2D<float> gOutputShadow : register(u0);

cbuffer TemporalConstants : register(b0)
{
    int gScreenWidth;
    int gScreenHeight;
    float gHistoryAlpha; // 通常時のブレンド係数（例: 0.15 = 85% 履歴採用）
    float gDisableHistory; // 1.0 で履歴を完全無効化（初回フレーム用）
    float gPadding[4];
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    int2 coord = int2(dispatchID.xy);
    if (coord.x >= gScreenWidth || coord.y >= gScreenHeight)
        return;

    // 背景ピクセル: 影なし
    float4 cWorldPos = gGBufferWorldPos.Load(int3(coord, 0));
    if (cWorldPos.a < 0.5f)
    {
        gOutputShadow[coord] = 1.0f;
        return;
    }

    float3 cNormal = normalize(gGBufferNormal.Load(int3(coord, 0)).rgb * 2.0f - 1.0f);
    float cDepth = length(cWorldPos.xyz);

    // 法線・深度ガイドの 3×3 空間前処理
    //   バイナリを準連続値に変換しつつ、近傍範囲 [cMin, cMax] を取得
    float sum = 0.0f;
    float weightSum = 0.0f;
    float cMin = 1.0f;
    float cMax = 0.0f;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 nc = clamp(coord + int2(dx, dy),
                            int2(0, 0),
                            int2(gScreenWidth - 1, gScreenHeight - 1));

            float4 nwp = gGBufferWorldPos.Load(int3(nc, 0));
            if (nwp.a < 0.5f)
                continue;

            float3 nNormal = normalize(gGBufferNormal.Load(int3(nc, 0)).rgb * 2.0f - 1.0f);
            float nDepth = length(nwp.xyz);

            // 幾何類似度
            float wNormal = pow(max(0.0f, dot(cNormal, nNormal)), 32.0f);
            float wDepth = exp(-abs(cDepth - nDepth) * 2.0f);
            float w = wNormal * wDepth;

            float s = gRawShadow.Load(int3(nc, 0));
            sum += s * w;
            weightSum += w;

            // 幾何的に似た近傍のみをクランプ対象にする
            if (w > 0.5f)
            {
                cMin = min(cMin, s);
                cMax = max(cMax, s);
            }
        }
    }

    float filteredCurrent = (weightSum > 1e-6f) ? (sum / weightSum) : 0.0f;

    // 幾何類似近傍が見つからないケース（孤立ピクセル）は自分自身で補完
    if (cMax < cMin)
    {
        float self = gRawShadow.Load(int3(coord, 0));
        cMin = self;
        cMax = self;
    }

    // モーションベクターで前フレームに再投影（NDC差分 → ピクセル差分）
    //   NDC Y はスクリーン Y と逆向きなので符号反転
    float2 mv = gMotionVector.Load(int3(coord, 0));
    float2 prevF;
    prevF.x = float(coord.x) - mv.x * float(gScreenWidth) * 0.5f;
    prevF.y = float(coord.y) + mv.y * float(gScreenHeight) * 0.5f;
    int2 prevPixel = int2(round(prevF));

    bool inBounds = prevPixel.x >= 0 && prevPixel.y >= 0
                 && prevPixel.x < gScreenWidth
                 && prevPixel.y < gScreenHeight;

    float output;
    if (inBounds && gDisableHistory < 0.5f)
    {
        float history = gHistoryShadow.Load(int3(prevPixel, 0));

        // SVariance Clamping
        //   history を現フレーム近傍範囲にクランプし、クランプ量を
        //   ディスオクルージョン信号として使う。
        float clampedHistory = clamp(history, cMin, cMax);
        float clampDelta = abs(history - clampedHistory);

        // クランプ量 0.1 で完全棄却
        float disocclusionWeight = saturate(clampDelta / 0.1f);
        float alpha = lerp(gHistoryAlpha, 1.0f, disocclusionWeight);

        output = lerp(clampedHistory, filteredCurrent, alpha);
    }
    else
    {
        // 履歴無効（初回フレーム or 再投影画面外）: 空間前処理結果をそのまま
        output = filteredCurrent;
    }

    gOutputShadow[coord] = output;
}
