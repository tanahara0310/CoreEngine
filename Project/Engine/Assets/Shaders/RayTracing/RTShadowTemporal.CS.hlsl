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
//   R8G8: R = シャドウ値, G = 蓄積フレーム数 N / 255（適応ブレンド用）
Texture2D<float2> gHistoryShadow : register(t3);

// モーションベクター（NDC差分、GBuffer 産）※フル解像度
Texture2D<float2> gMotionVector : register(t4);

// 出力: テンポラル蓄積済みシャドウ（＝次フレームの履歴）※トレース解像度
//   R8G8: R = シャドウ値, G = 蓄積フレーム数 N / 255
RWTexture2D<float2> gOutputShadow : register(u0);

cbuffer TemporalConstants : register(b0)
{
    int gTraceWidth;
    int gTraceHeight;
    // 蓄積フレーム数の上限（適応ブレンドの収束下限 = 1/gMaxHistoryFrames）。
    // 旧実装は固定ブレンド係数 gHistoryAlpha だったが、固定 α の EMA は定常状態でも
    // 入力ノイズの sqrt(α/(2-α)) が永久に残る（α=0.3 で 42%）。静止カメラでも
    // 影のペナンブラが毎フレームちらつく実測の主因だったため、ピクセルごとの
    // 蓄積カウント N による α=1/N（SVGF 方式）へ変更した。
    float gMaxHistoryFrames;
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

/// @brief トレース座標 → G-Buffer 参照用のフル解像度ピクセル座標
/// @details RayGen が実際に踏んだサブピクセル（gTraceOffset）ではなく、2x2 の固定代表点を使う。
///          巡回オフセットをそのまま使うとガイド（深度・法線）が毎フレーム別のピクセルになり、
///          幾何重みとクランプ範囲が静止シーンでも 4 フレーム周期で揺れてちらつきの一因になる。
int2 TraceToFull(int2 traceCoord)
{
    int2 full = traceCoord * gTraceScale + (gTraceScale >> 1);
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
        gOutputShadow[coord] = float2(1.0f, 0.0f);
        return;
    }

    float3 cNormal = normalize(gGBufferNormal.Load(int3(TraceToFull(coord), 0)).rgb * 2.0f - 1.0f);
    // 深度差しか使わないので線形ビュー深度で足りる（旧: ワールド座標復元 + length）
    float cDepth = LinearizeViewDepth(cNdcDepth, projZW);

    // 法線・深度ガイドの 3×3 空間前処理（近傍はトレース解像度で取る）
    //   バイナリを準連続値に変換しつつ、クランプ用の 1 次・2 次モーメントを取る
    float sum = 0.0f;
    float sumSq = 0.0f;
    float weightSum = 0.0f;

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
            // 深度は相対差で見る（Resolve と同じ理由: 絶対差 [m] だと遠景やかすめ角の
            // 地面は隣接ピクセル同士でも数 m 差になり、全近傍の重みが消える。
            // その結果クランプ範囲が中心の 1spp バイナリ値へ退化し、履歴が毎フレーム
            // 完全棄却されて静止カメラでも影の中に生ノイズが素通りしていた）
            float wNormal = pow(max(0.0f, dot(cNormal, nNormal)), 32.0f);
            float relDepthDiff = abs(cDepth - nDepth) / max(cDepth, 1e-3f);
            float wDepth = exp(-relDepthDiff * 16.0f);
            float w = wNormal * wDepth;

            float s = gRawShadow.Load(int3(nc, 0));
            sum += s * w;
            sumSq += s * s * w;
            weightSum += w;
        }
    }

    float filteredCurrent = (weightSum > 1e-6f) ? (sum / weightSum) : 0.0f;

    // Variance Clipping（モーメント方式）
    //   旧実装は「幾何類似近傍の生値 min/max」を範囲にしていたが、1spp のバイナリ生値では
    //   影の内側で近傍がたまたま全部 0 の フレームに範囲が [0,0] へ潰れ、収束済みの履歴
    //   （例: ジッターの当たり率 0.1 の平均値）が毎回棄却されてしまう。棄却＝蓄積カウントの
    //   リセットなので、適応ブレンドがいつまでも収束しない主因だった。
    //   μ ± γσ に固定マージン ε を足した範囲なら、ノイズ由来の揺らぎは範囲内に収まり、
    //   本当に影が変わったとき（μ が大きく動く）は従来どおり棄却される。
    const float kClampGamma = 1.25f;
    const float kClampMargin = 0.05f;
    float mean = filteredCurrent;
    float variance = (weightSum > 1e-6f) ? max(0.0f, sumSq / weightSum - mean * mean) : 0.0f;
    float extent = kClampGamma * sqrt(variance) + kClampMargin;
    float cMin = saturate(mean - extent);
    float cMax = saturate(mean + extent);

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
    float outN; // 今フレームまでの蓄積フレーム数
    if (inBounds && gDisableHistory < 0.5f)
    {
        float2 hist = gHistoryShadow.Load(int3(prevPixel, 0));
        float history = hist.x;
        float historyN = hist.y * 255.0f;

        // Variance Clamping
        //   history を現フレーム近傍範囲にクランプし、クランプ量を
        //   ディスオクルージョン信号として使う。
        float clampedHistory = clamp(history, cMin, cMax);
        float clampDelta = abs(history - clampedHistory);

        // クランプ量 0.1 で完全棄却（＝蓄積カウントもリセットして即応させる）
        float disocclusionWeight = saturate(clampDelta / 0.1f);
        historyN *= (1.0f - disocclusionWeight);

        // 適応ブレンド: α = 1/N。蓄積が進むほど新フレームの寄与（＝ノイズ）が減り、
        // 静止シーンでは 1/gMaxHistoryFrames まで収束する。棄却直後は N≈1 で α≈1。
        outN = min(historyN + 1.0f, gMaxHistoryFrames);
        float alpha = 1.0f / outN;

        output = lerp(clampedHistory, filteredCurrent, alpha);
    }
    else
    {
        // 履歴無効（初回フレーム or 再投影画面外）: 空間前処理結果をそのまま
        output = filteredCurrent;
        outN = 1.0f;
    }

    gOutputShadow[coord] = float2(output, outN / 255.0f);
}
