// ============================================================
// RT シャドウ テンポラル蓄積（SVGF 準拠）
//   RayGen の生バイナリを 5×5 幾何ガイドで前処理し、
//   バイリニア再投影 + 深度による棄却判定 + 統計的クランプで履歴とブレンドする。
//   RayGen → このパス → A-Trous → 解決（アップサンプル）の順。
//
//   このパスは「トレース解像度」で動く。履歴は 2 枚の ping-pong で、
//   出力先がそのまま次フレームの履歴になる。
//
//   出力チャンネル（履歴・A-Trous 中間バッファ共通のレイアウト）
//     .x = シャドウ値（0=影, 1=光）
//     .y = そのシャドウ値がもつ推定分散（A-Trous のエッジ停止関数を駆動する）
//     .z = 蓄積フレーム数 N（適応ブレンド α = 1/N）
//     .w = 書き込み時点の線形ビュー深度（次フレームの再投影検証に使う）
// ============================================================

#include "../Include/Common/DepthReconstruction.hlsli"

// 入力: RayGen の生出力（x=シャドウ値, y=そのピクセルに撃ったレイ本数）※トレース解像度
Texture2D<float2> gRawShadow : register(t0);

// G-Buffer: 法線（空間前処理の幾何ガイド用）※フル解像度
Texture2D<float4> gGBufferNormal : register(t1);

// G-Buffer: 深度（深度ガイド・背景判定用）※フル解像度
Texture2D<float> gGBufferDepth : register(t2);

// 前フレームの Temporal 出力 ※トレース解像度（チャンネル定義は冒頭のコメント参照）
Texture2D<float4> gHistoryShadow : register(t3);

// モーションベクター（NDC差分、GBuffer 産）※フル解像度
Texture2D<float2> gMotionVector : register(t4);

// 出力: テンポラル蓄積済みシャドウ（＝次フレームの履歴）※トレース解像度
RWTexture2D<float4> gOutputShadow : register(u0);

cbuffer TemporalConstants : register(b0)
{
    int gTraceWidth;
    int gTraceHeight;
    // 蓄積フレーム数の上限（適応ブレンドの収束下限 = 1/gMaxHistoryFrames）。
    // ピクセルごとの蓄積カウント N による α=1/N（SVGF 方式）。
    float gMaxHistoryFrames;
    float gDisableHistory; // 1.0 で履歴を完全無効化（初回フレーム用）
    // 深度重みに使う線形化パラメータ float2(proj._33, proj._43)。
    float gProjM33;
    float gProjM43;
    int gTraceScale; // 1 = フル解像度 / 2 = ハーフ解像度
    int gTraceOffsetX;
    int gTraceOffsetY;
    int gFullWidth;
    int gFullHeight;
    // クランプ帯の広さ = gClampSigmaScale * σ + gClampMargin。
    // σ は「推定値がもつ標準偏差」なので、モンテカルロ由来の揺らぎは必ず帯に収まる。
    float gClampSigmaScale;
    float gClampMargin;
    // 再投影先の履歴を採用するかを決める相対深度しきい値（カメラのドリー移動ぶんの余裕）
    float gDepthTolerance;
    // 静止化しきい値（σ の倍数）。|現フレーム推定 - 履歴| がこの倍数×σ 未満のとき、
    // その差は統計的にノイズと区別できないので履歴をそのまま出力する（=画面が完全に止まる）。
    // 0 で無効。
    float gStillnessSigma;
    int gPad1_;
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

/// @brief 履歴の .w へ格納する深度へ丸める
/// @details 履歴は R16G16B16A16_FLOAT なので float16 の上限 65504 を超えると +inf になる。
///          far を 100000 に取る構成では遠景がそのまま inf 化し、
///          「inf - inf = NaN」で再投影検証が常に失敗して履歴が捨てられる。
///          比較は相対値でしか見ないので、上限で頭打ちにして構わない。
float EncodeHistoryDepth(float viewDepth)
{
    return min(viewDepth, 60000.0f);
}

/// @brief 指定トレース座標の線形ビュー深度を読む（背景なら負値を返す）
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

    // 背景ピクセル: 影なし
    float cNdcDepth = gGBufferDepth.Load(int3(TraceToFull(coord), 0));
    if (IsBackgroundDepth(cNdcDepth))
    {
        gOutputShadow[coord] = float4(1.0f, 0.0f, 0.0f, -1.0f);
        return;
    }

    float3 cNormal = normalize(gGBufferNormal.Load(int3(TraceToFull(coord), 0)).rgb * 2.0f - 1.0f);
    // 深度差しか使わないので線形ビュー深度で足りる
    float cDepth = EncodeHistoryDepth(LinearizeViewDepth(cNdcDepth, projZW));

    // ===== 5×5 幾何ガイド空間前処理 =====
    //   バイナリを準連続値に変換する。3×3（実効 9 サンプル）から 5×5（実効 25 サンプル）へ
    //   広げると、1spp のショットノイズが 1/sqrt(25/9) ≒ 0.6 倍になる。トレース解像度での
    //   25 タップなのでコストは小さい。
    //   同時に「実効サンプル数（Kish）」を求める。後段のクランプ帯と分散推定は
    //   この実効サンプル数から解析的に決まる。
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

            // 幾何類似度
            //   深度は相対差で見る（絶対差 [m] だと遠景やかすめ角の地面は隣接ピクセル同士でも
            //   数 m 差になり、全近傍の重みが消える）。距離で正規化するのは、平面上では
            //   隣接差がタップ距離に比例して増えるため。
            float taxicab = float(max(abs(dx), abs(dy)));
            float wNormal = pow(max(0.0f, dot(cNormal, nNormal)), 16.0f);
            float relDepthDiff = abs(cDepth - nDepth) / (max(cDepth, 1e-3f) * max(taxicab, 1.0f));
            float wDepth = exp(-relDepthDiff * 16.0f);
            // レイ本数 n のタップは分散が 1/n なので、最適重みは幾何重み × n。
            // 実効サンプル数（下）も本数込みで正しく数えられる。
            float2 raw = gRawShadow.Load(int3(nc, 0));
            float rayCount = max(raw.y, 1.0f);
            float w = wNormal * wDepth * rayCount;

            sum += raw.x * w;
            weightSum += w;
            weightSqSum += w * w / rayCount;
        }
    }

    float filteredCurrent = (weightSum > 1e-6f) ? (sum / weightSum) : gRawShadow.Load(int3(coord, 0)).x;
    // Kish の実効サンプル数（レイ本数込み）。5x5 全タップ等重み × 16 本が理論上限。
    float effectiveSamples = (weightSqSum > 1e-12f)
        ? clamp((weightSum * weightSum) / weightSqSum, 1.0f, 400.0f)
        : 1.0f;

    // ===== 推定値がもつ分散（ラプラス平滑つきベルヌーイ分散）=====
    //   旧実装は「近傍の実測ばらつき」sumSq/weightSum - mean^2 を σ にしていたが、
    //   1spp のバイナリでは近傍がたまたま全部 0（または全部 1）になる確率が高く、
    //   そのフレームだけ σ=0 と報告してクランプ帯が固定マージン 0.05 まで潰れる。
    //   すると収束済みの履歴（例 0.08）が毎フレーム 0 へクランプされ、
    //   蓄積カウントもリセットされる＝テンポラル蓄積が原理的に収束できなかった。
    //   これが「デノイズしているのに影の縁にノイズが残る」最大の原因。
    //   n 個の 0/1 から母比率を推定するときの分散は、観測が全一致でもゼロではない。
    //   ラプラス平滑 p' = (k+1)/(n+2) を使えば必ず正の分散が出る。
    float smoothedP = (filteredCurrent * effectiveSamples + 1.0f) / (effectiveSamples + 2.0f);
    float sampleVariance = smoothedP * (1.0f - smoothedP);
    float varianceCurrent = sampleVariance / effectiveSamples; // 今フレームの推定値の分散

    // ===== 深度勾配（再投影の検証しきい値に使う）=====
    //   カメラが前後に動けば同じ面でも深度は変わる。斜面ではさらにピクセル単位で変わるので、
    //   「相対しきい値」と「勾配 × 移動ピクセル数」の大きいほうを許容幅にする。
    //   背景（負値）の隣接は勾配に含めない。含めると輪郭のところで見かけの勾配が
    //   depth+1 まで跳ね上がり、許容幅が無限に広がって輪郭でこそ効くべき検証が無効になる。
    float depthRight = LoadTraceDepth(coord + int2(1, 0), projZW);
    float depthDown = LoadTraceDepth(coord + int2(0, 1), projZW);
    float depthGradient = 0.0f;
    if (depthRight >= 0.0f) depthGradient = max(depthGradient, abs(depthRight - cDepth));
    if (depthDown >= 0.0f) depthGradient = max(depthGradient, abs(depthDown - cDepth));

    // ===== モーションベクターで前フレームに再投影 =====
    //   モーションベクターは NDC 差分なので解像度非依存。掛ける寸法だけトレース側にする。
    //   NDC Y はスクリーン Y と逆向きなので符号反転。
    float2 mv = gMotionVector.Load(int3(TraceToFull(coord), 0));
    float2 prevF;
    prevF.x = float(coord.x) - mv.x * float(gTraceWidth) * 0.5f;
    prevF.y = float(coord.y) + mv.y * float(gTraceHeight) * 0.5f;
    float motionPixels = length(prevF - float2(coord));

    // ===== 再投影誤差ぶんの深度許容幅 =====
    //   勾配で見逃していいのは「再投影先がテクセル中心からずれているぶん」だけで、
    //   その誤差はバイリニア 2x2 の広がり（±1 テクセル）とモーションベクターの
    //   量子化しかない。移動量に比例させると、速い移動や誤ったモーションベクターの
    //   ところで許容幅が青天井に広がり、ディスオクルージョン検証が事実上無効になる
    //   （＝別の面の履歴をそのまま採用して、動いている間だけ影がちらつく）。
    //   数テクセルで頭打ちにしておけば、正しい再投影は今までどおり通り、
    //   嘘のモーションベクターは深度で弾かれて空間前処理結果へフォールバックする。
    const float kMaxReprojectionTexels = 2.0f;
    float reprojectionTexels = min(motionPixels, kMaxReprojectionTexels) + 1.0f;
    float depthTolerance = max(gDepthTolerance * cDepth, depthGradient * reprojectionTexels);

    // ===== バイリニア履歴取得 =====
    //   旧実装は round() の最近傍取得だった。サブテクセルのずれが必ず最大 0.5 テクセル残るため、
    //   影の縁では履歴と現フレームが別の値になり、クランプで棄却 → 蓄積カウントがリセット →
    //   α=1 で 1spp の生バイナリがそのまま出力される。カメラを動かした瞬間に縁のノイズが
    //   跳ね上がるのはこれが原因。2x2 をバイリニア重みで取り、タップごとに深度で検証する。
    //   座標規約に注意: prevF は「テクセル番号」空間で、テクセル i の中心はちょうど i。
    //   UV 空間（テクセル i が [i, i+1) を覆い中心が i+0.5）と混同して 0.5 を引くと、
    //   カメラが完全に静止していても frac が 0.5 に固定され、毎フレーム履歴を
    //   半テクセルずらした 2x2 ボックスで畳み込むことになる。影の縁が毎フレーム
    //   にじみながら移動し続ける（＝縁が這って見える）ので絶対に引かないこと。
    int2 baseTap = int2(floor(prevF));
    float2 frac2 = prevF - float2(baseTap);

    float bilinearWeights[4] = {
        (1.0f - frac2.x) * (1.0f - frac2.y),
        frac2.x * (1.0f - frac2.y),
        (1.0f - frac2.x) * frac2.y,
        frac2.x * frac2.y
    };
    int2 tapOffsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };

    float historyShadow = 0.0f;
    float historyN = 0.0f;
    float historyWeight = 0.0f;

    [unroll]
    for (int t = 0; t < 4; ++t)
    {
        int2 tap = baseTap + tapOffsets[t];
        if (tap.x < 0 || tap.y < 0 || tap.x >= gTraceWidth || tap.y >= gTraceHeight)
            continue;

        float4 h = gHistoryShadow.Load(int3(tap, 0));
        // .w には書き込み時点の線形深度が入っている。背景だったテクセルは負値。
        if (h.w < 0.0f || abs(h.w - cDepth) > depthTolerance)
            continue;

        float w = bilinearWeights[t];
        historyShadow += h.x * w;
        historyN += h.z * w;
        historyWeight += w;
    }

    float output;
    float outN;
    if (historyWeight > 1e-4f && gDisableHistory < 0.5f)
    {
        historyShadow /= historyWeight;
        historyN /= historyWeight;

        // ===== 統計的クランプ =====
        //   履歴も現フレームも「真の遮蔽率の推定値」なので、両者の差がそれぞれの
        //   標準偏差の合成より小さいうちは、差はノイズであって影の変化ではない。
        //   帯を σ で決めることで、ノイズでは絶対に棄却されず、影が本当に動いたときだけ
        //   棄却されるようになる（旧実装の固定マージン 0.05 は前者を棄却していた）。
        float varianceHistory = varianceCurrent / max(historyN, 1.0f);
        float extent = gClampSigmaScale * sqrt(varianceCurrent + varianceHistory) + gClampMargin;
        float clampedHistory = clamp(historyShadow,
                                     saturate(filteredCurrent - extent),
                                     saturate(filteredCurrent + extent));

        // ===== 蓄積カウントを削る条件 =====
        //   帯からのはみ出しに「比例して」削ると、はみ出しが小さくても毎フレーム少しずつ N が
        //   減り、+1 の増加と釣り合ったところ（実測で N≒17）で頭打ちになる。α=1/17 は
        //   影の縁のノイズを消すには大きすぎ、これが「縁だけ這うように動く」直接の原因だった。
        //   本来 N を落とすべきなのは「統計では説明できないほど大きく変わったとき」だけ。
        //   はみ出しが帯幅と同等（＝合計 2 倍の帯の外）になって初めて削り始める。
        //   影が本当に動いたときの追従は N ではなくクランプ自体が担う（履歴は必ず
        //   現フレーム推定 ± 帯 の中へ引き込まれるので、1 フレームで追いつく）。
        float excess = abs(historyShadow - clampedHistory);
        float rejection = saturate(excess / max(extent, 1e-4f) - 1.0f);
        historyN *= (1.0f - rejection);

        outN = min(historyN + 1.0f, gMaxHistoryFrames);

        // ===== 静止化（stillness deadband）=====
        //   α=1/N の EMA は定常状態でも毎フレーム α×(新フレームのノイズ) だけ動き続け、
        //   これが「影の縁がごにょごにょ這う」低周波のうねりとして見える（EMA はローパスなので
        //   残差は必ずゆっくり動く成分になり、目が非常に拾いやすい）。
        //   |現フレーム推定 - 履歴| が両者の標準偏差の合成より小さいとき、その差は
        //   統計的にノイズと区別できない＝更新しても情報が増えないので、履歴を
        //   そのまま出力する。静止シーンでは出力がビット単位で止まる。
        //   本物の変化（差が σ を超える）は今までどおり即ブレンドされる。
        float stillnessBand = gStillnessSigma * sqrt(varianceCurrent + varianceHistory);
        if (abs(filteredCurrent - clampedHistory) < stillnessBand)
        {
            output = clampedHistory;
        }
        else
        {
            // 適応ブレンド α = 1/N。蓄積が進むほど新フレームの寄与（＝ノイズ）が減る。
            output = lerp(clampedHistory, filteredCurrent, 1.0f / outN);
        }
    }
    else
    {
        // 履歴無効（初回フレーム / 画面外 / ディスオクルージョン）: 空間前処理結果をそのまま
        output = filteredCurrent;
        outN = 1.0f;
    }

    // 出力値そのものの分散。N フレームぶん平均しているので 1 フレームぶんの 1/N。
    // A-Trous はこの値からエッジ停止関数の σ を作る。蓄積が浅いピクセル（＝ノイジー）ほど
    // 大きくなるので、ディスオクルージョン直後だけ強くぼけるという SVGF の挙動になる。
    float outputVariance = varianceCurrent / outN;

    gOutputShadow[coord] = float4(output, outputVariance, outN, cDepth);
}
