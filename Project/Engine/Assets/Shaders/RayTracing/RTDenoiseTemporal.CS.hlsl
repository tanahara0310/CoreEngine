// RT デノイザの時間方向の蓄積（トレース解像度で動き、出力が次のフレームの履歴になる）

#include "../Include/Common/DepthReconstruction.hlsli"

// 入力：x = 値（0〜1 の割合）、y = サンプル数（0 は信号の無い画素）※トレース解像度
Texture2D<float2> gRawSignal : register(t0);

// G-Buffer の法線 ※フル解像度
Texture2D<float4> gGBufferNormal : register(t1);

// G-Buffer の深度 ※フル解像度
Texture2D<float> gGBufferDepth : register(t2);

// 前フレームの出力 ※トレース解像度
Texture2D<float4> gHistorySignal : register(t3);

// モーションベクター（NDC の差分）※フル解像度
Texture2D<float2> gMotionVector : register(t4);

// 出力：x = 値、y = 値の推定分散、z = 蓄積フレーム数 N（0 は信号の無い画素）、w = 線形ビュー深度（背景は負）
// ※トレース解像度
RWTexture2D<float4> gOutputSignal : register(u0);

cbuffer RTDenoiseTemporalConstants : register(b0)
{
    int gTraceWidth;
    int gTraceHeight;
    float gMaxHistoryFrames; // 蓄積フレーム数の上限（混ぜる割合 α = 1/N の下限を決める）
    float gDisableHistory;   // 1 で履歴を読まない
    float gProjM33;          // 深度の線形化に使う proj._33
    float gProjM43;          // 深度の線形化に使う proj._43
    int gTraceScale;         // 1 = フル解像度 / 2 = ハーフ解像度
    int gPad0_;
    int gPad1_;
    int gFullWidth;
    int gFullHeight;
    float gClampSigmaScale;  // クランプ帯の広さ = gClampSigmaScale × σ + gClampMargin
    float gClampMargin;
    float gDepthTolerance;   // 再投影した履歴を採用する相対深度しきい値
    float gStillnessSigma;   // 現フレームと履歴の差がこの倍数 × σ 未満なら履歴をそのまま出す（0 で無効）
    int gPad2_;
};

/// @brief トレース座標 → G-Buffer を読むフル解像度の座標（2x2 の中央の固定の代表点）
int2 TraceToFull(int2 traceCoord)
{
    int2 full = traceCoord * gTraceScale + (gTraceScale >> 1);
    return min(full, int2(gFullWidth - 1, gFullHeight - 1));
}

/// @brief 履歴の .w に入れる深度（float16 の上限を超えないよう頭打ちにする）
float EncodeHistoryDepth(float viewDepth)
{
    return min(viewDepth, 60000.0f);
}

/// @brief トレース座標の線形ビュー深度（背景は負）
float LoadTraceDepth(int2 traceCoord, float2 projZW)
{
    int2 clamped = clamp(traceCoord, int2(0, 0), int2(gTraceWidth - 1, gTraceHeight - 1));
    float ndc = gGBufferDepth.Load(int3(TraceToFull(clamped), 0));
    return IsBackgroundDepth(ndc) ? -1.0f : EncodeHistoryDepth(LinearizeViewDepth(ndc, projZW));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    int2 coord = int2(dispatchID.xy);
    if (coord.x >= gTraceWidth || coord.y >= gTraceHeight)
        return;

    const float2 projZW = float2(gProjM33, gProjM43);

    // 背景：値 1・蓄積数 0
    float cNdcDepth = gGBufferDepth.Load(int3(TraceToFull(coord), 0));
    if (IsBackgroundDepth(cNdcDepth))
    {
        gOutputSignal[coord] = float4(1.0f, 0.0f, 0.0f, -1.0f);
        return;
    }

    float cDepth = EncodeHistoryDepth(LinearizeViewDepth(cNdcDepth, projZW));

    // 信号の無い画素：値 1・蓄積数 0（次に信号が来たフレームは履歴を使わずに始め直す）
    if (gRawSignal.Load(int3(coord, 0)).y <= 0.0f)
    {
        gOutputSignal[coord] = float4(1.0f, 0.0f, 0.0f, cDepth);
        return;
    }

    float3 cNormal = normalize(gGBufferNormal.Load(int3(TraceToFull(coord), 0)).rgb * 2.0f - 1.0f);

    // ===== 5×5 の幾何ガイドで空間の前処理 =====
    //   深度と法線が近い近傍を、サンプル数で重み付けして平均する。
    //   同時に重みの和と 2 乗和から実効サンプル数（Kish）を求める。
    float sum = 0.0f;
    float weightSum = 0.0f;
    float weightSqSum = 0.0f;

    [unroll]
    for (int dy = -2; dy <= 2; ++dy)
    {
        [unroll]
        for (int dx = -2; dx <= 2; ++dx)
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

            // 深度は相対差をタップの距離で割って見る
            float taxicab = float(max(abs(dx), abs(dy)));
            float wNormal = pow(max(0.0f, dot(cNormal, nNormal)), 16.0f);
            float relDepthDiff = abs(cDepth - nDepth) / (max(cDepth, 1e-3f) * max(taxicab, 1.0f));
            float wDepth = exp(-relDepthDiff * 16.0f);
            // サンプル数 n のタップは分散が 1/n なので、重みは幾何の重み × n
            float2 raw = gRawSignal.Load(int3(nc, 0));
            if (raw.y <= 0.0f)
                continue; // 信号の無い画素
            float rayCount = max(raw.y, 1.0f);
            float w = wNormal * wDepth * rayCount;

            sum += raw.x * w;
            weightSum += w;
            weightSqSum += w * w / rayCount;
        }
    }

    float filteredCurrent = (weightSum > 1e-6f) ? (sum / weightSum) : gRawSignal.Load(int3(coord, 0)).x;
    float effectiveSamples = (weightSqSum > 1e-12f)
        ? clamp((weightSum * weightSum) / weightSqSum, 1.0f, 400.0f)
        : 1.0f;

    // ===== 今フレームの推定値の分散 =====
    //   実効サンプル数ぶんの 0/1 から割合を推定したときの分散を、ラプラス平滑 p' = (k+1)/(n+2) で求める。
    float smoothedP = (filteredCurrent * effectiveSamples + 1.0f) / (effectiveSamples + 2.0f);
    float sampleVariance = smoothedP * (1.0f - smoothedP);
    float varianceCurrent = sampleVariance / effectiveSamples;

    // ===== 深度の勾配（背景の隣接は含めない）=====
    float depthRight = LoadTraceDepth(coord + int2(1, 0), projZW);
    float depthDown = LoadTraceDepth(coord + int2(0, 1), projZW);
    float depthGradient = 0.0f;
    if (depthRight >= 0.0f) depthGradient = max(depthGradient, abs(depthRight - cDepth));
    if (depthDown >= 0.0f) depthGradient = max(depthGradient, abs(depthDown - cDepth));

    // ===== モーションベクターで前フレームへ再投影 =====
    //   NDC の Y はスクリーンの Y と逆向き。
    float2 mv = gMotionVector.Load(int3(TraceToFull(coord), 0));
    float2 prevF;
    prevF.x = float(coord.x) - mv.x * float(gTraceWidth) * 0.5f;
    prevF.y = float(coord.y) + mv.y * float(gTraceHeight) * 0.5f;
    float motionPixels = length(prevF - float2(coord));

    // ===== 履歴を採用する深度の許容幅 =====
    //   相対しきい値と「勾配 × 再投影の誤差のテクセル数（2 で頭打ち）」の大きいほう。
    const float kMaxReprojectionTexels = 2.0f;
    float reprojectionTexels = min(motionPixels, kMaxReprojectionTexels) + 1.0f;
    float depthTolerance = max(gDepthTolerance * cDepth, depthGradient * reprojectionTexels);

    // ===== 履歴を 2x2 のバイリニアで読む =====
    //   prevF はテクセル番号の空間（テクセル i の中心が i）。タップごとに深度で確かめる。
    int2 baseTap = int2(floor(prevF));
    float2 frac2 = prevF - float2(baseTap);

    float bilinearWeights[4] = {
        (1.0f - frac2.x) * (1.0f - frac2.y),
        frac2.x * (1.0f - frac2.y),
        (1.0f - frac2.x) * frac2.y,
        frac2.x * frac2.y
    };
    int2 tapOffsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };

    float historyValue = 0.0f;
    float historyN = 0.0f;
    float historyWeight = 0.0f;

    [unroll]
    for (int t = 0; t < 4; ++t)
    {
        int2 tap = baseTap + tapOffsets[t];
        if (tap.x < 0 || tap.y < 0 || tap.x >= gTraceWidth || tap.y >= gTraceHeight)
            continue;

        float4 h = gHistorySignal.Load(int3(tap, 0));
        // 背景だった・信号が無かった・深度が合わないタップは使わない
        if (h.w < 0.0f || h.z <= 0.0f || abs(h.w - cDepth) > depthTolerance)
            continue;

        float w = bilinearWeights[t];
        historyValue += h.x * w;
        historyN += h.z * w;
        historyWeight += w;
    }

    float output;
    float outN;
    if (historyWeight > 1e-4f && gDisableHistory < 0.5f)
    {
        historyValue /= historyWeight;
        historyN /= historyWeight;

        // ===== 推定分散によるクランプ =====
        //   帯 = gClampSigmaScale × (現フレームと履歴の標準偏差の合成) + gClampMargin。
        float varianceHistory = varianceCurrent / max(historyN, 1.0f);
        float extent = gClampSigmaScale * sqrt(varianceCurrent + varianceHistory) + gClampMargin;
        float clampedHistory = clamp(historyValue,
                                     saturate(filteredCurrent - extent),
                                     saturate(filteredCurrent + extent));

        // ===== 蓄積数を削る =====
        //   帯からのはみ出しが帯の幅を超えたぶんに比例して N を減らす。
        float excess = abs(historyValue - clampedHistory);
        float rejection = saturate(excess / max(extent, 1e-4f) - 1.0f);
        historyN *= (1.0f - rejection);

        outN = min(historyN + 1.0f, gMaxHistoryFrames);

        // ===== 静止 =====
        //   現フレームと履歴の差が gStillnessSigma × σ 未満なら履歴をそのまま出し、超えたら α = 1/N で混ぜる。
        float stillnessBand = gStillnessSigma * sqrt(varianceCurrent + varianceHistory);
        if (abs(filteredCurrent - clampedHistory) < stillnessBand)
        {
            output = clampedHistory;
        }
        else
        {
            output = lerp(clampedHistory, filteredCurrent, 1.0f / outN);
        }
    }
    else
    {
        // 履歴が無い（初めてのフレーム・画面の外・隠れていた所）：空間の前処理の結果をそのまま
        output = filteredCurrent;
        outN = 1.0f;
    }

    // 出力の分散（N フレームの平均なので 1 フレームぶんの 1/N）。A-Trous のエッジ停止関数が読む
    float outputVariance = varianceCurrent / outN;

    gOutputSignal[coord] = float4(output, outputVariance, outN, cDepth);
}
