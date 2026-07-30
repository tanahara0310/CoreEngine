// ============================================================
// RT シャドウ テンポラル蓄積（SVGF 風）
//   RayGen の生バイナリを 3×3 幾何ガイドで前処理し、
//   再投影 + Variance Clamping で履歴とブレンドする。
//   RayGen → このパス → A-Trous → 解決（アップサンプル）の順。
//
//   Stage 3 以降、このパスは「トレース解像度」で動く。
//   履歴は 2 枚の ping-pong になったため、出力先がそのまま次フレームの履歴になる
//   （全画面 CopyResource は廃止）。
// ============================================================

#include "../Include/Common/DepthReconstruction.hlsli"

// 入力: RayGen の生バイナリ出力（0=影, 1=光）※トレース解像度
Texture2D<float> gRawShadow : register(t0);

// G-Buffer: 法線（空間前処理の幾何ガイド用）※フル解像度
Texture2D<float4> gGBufferNormal : register(t1);

// G-Buffer: 深度（深度ガイド・背景判定用）※フル解像度
Texture2D<float> gGBufferDepth : register(t2);

// 前フレームの Temporal 出力 ※トレース解像度
Texture2D<float> gHistoryShadow : register(t3);

// モーションベクター（NDC差分、GBuffer 産）※フル解像度
Texture2D<float2> gMotionVector : register(t4);

// 出力: テンポラル蓄積済みシャドウ（＝次フレームの履歴）※トレース解像度
RWTexture2D<float> gOutputShadow : register(u0);

cbuffer TemporalConstants : register(b0)
{
    int gTraceWidth;
    int gTraceHeight;
    float gHistoryAlpha; // 通常時のブレンド係数（例: 0.15 = 85% 履歴採用）
    float gDisableHistory; // 1.0 で履歴を完全無効化（初回フレーム用）
    // 深度重みに使う線形化パラメータ float2(proj._33, proj._43)。
    // 以前は gInvViewProj(16 float) で 1px あたり 10 回ワールド座標を復元していた。
    // 深度差しか要らないので線形ビュー深度で足りる。C++ 側 TemporalConstants と要一致。
    float gProjM33;
    float gProjM43;
    int gTraceScale; // 1 = フル解像度 / 2 = ハーフ解像度
    int gTraceOffsetX;
    int gTraceOffsetY;
    int gFullWidth;
    int gFullHeight;
    int gPad0_;
};

/// @brief トレース座標 → 対応するフル解像度ピクセル座標
int2 TraceToFull(int2 traceCoord)
{
    int2 full = traceCoord * gTraceScale + int2(gTraceOffsetX, gTraceOffsetY);
    return min(full, int2(gFullWidth - 1, gFullHeight - 1));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    int2 coord = int2(dispatchID.xy);
    if (coord.x >= gTraceWidth || coord.y >= gTraceHeight)
        return;

    const float2 projZW = float2(gProjM33, gProjM43);

    // 背景ピクセル: 影なし
    float cNdcDepth = gGBufferDepth.Load(int3(TraceToFull(coord), 0));
    if (IsBackgroundDepth(cNdcDepth))
    {
        gOutputShadow[coord] = 1.0f;
        return;
    }

    float3 cNormal = normalize(gGBufferNormal.Load(int3(TraceToFull(coord), 0)).rgb * 2.0f - 1.0f);
    // 深度差しか使わないので線形ビュー深度で足りる（旧: ワールド座標復元 + length）
    float cDepth = LinearizeViewDepth(cNdcDepth, projZW);

    // 法線・深度ガイドの 3×3 空間前処理（近傍はトレース解像度で取る）
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
                            int2(gTraceWidth - 1, gTraceHeight - 1));

            int2 nFull = TraceToFull(nc);
            float nNdcDepth = gGBufferDepth.Load(int3(nFull, 0));
            if (IsBackgroundDepth(nNdcDepth))
                continue;

            float3 nNormal = normalize(gGBufferNormal.Load(int3(nFull, 0)).rgb * 2.0f - 1.0f);
            float nDepth = LinearizeViewDepth(nNdcDepth, projZW);

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

    // モーションベクターで前フレームに再投影（NDC差分 → トレース解像度のピクセル差分）
    //   モーションベクターは NDC 差分なので解像度非依存。掛ける寸法だけトレース側にする。
    //   NDC Y はスクリーン Y と逆向きなので符号反転
    float2 mv = gMotionVector.Load(int3(TraceToFull(coord), 0));
    float2 prevF;
    prevF.x = float(coord.x) - mv.x * float(gTraceWidth) * 0.5f;
    prevF.y = float(coord.y) + mv.y * float(gTraceHeight) * 0.5f;
    int2 prevPixel = int2(round(prevF));

    bool inBounds = prevPixel.x >= 0 && prevPixel.y >= 0
                 && prevPixel.x < gTraceWidth
                 && prevPixel.y < gTraceHeight;

    float output;
    if (inBounds && gDisableHistory < 0.5f)
    {
        float history = gHistoryShadow.Load(int3(prevPixel, 0));

        // Variance Clamping
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
