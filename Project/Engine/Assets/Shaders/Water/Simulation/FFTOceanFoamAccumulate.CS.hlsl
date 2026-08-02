// ============================================================
// FFT Ocean 泡（whitecap）の蓄積・減衰パス
// ------------------------------------------------------------
// 泡は瞬時の砕波判定（detJ < bias）だけだと波の位相と一緒に動いて明滅する。
// このパスは前フレームの泡を指数減衰させつつ、今フレームの砕波候補を
// max 合成（Atlas方式）で書き込み、「砕けた後に白い筋が数秒残る」時間発展を作る。
//
//   foam = max(prevFoam * exp(-dt/τ), injection)
//
// 蓄積は FFT の参照格子（カスケード毎の周期テクスチャ）上で行う。深水波の
// 質点軌道はほぼ円運動（Stokesドリフトは微小）なので、格子テクセルに泡を
// 置けば移流なしでも変位後のサーフェスに乗って自然に運ばれる。
//
// 入力ヤコビアンは FFTOceanFinalize.CS の (Jxx, Jzz, Jxy, detJ) 形式。
// 発生判定はカスケード単体の「重み付き detJ」= det(I + w·J) を勾配成分から
// 正確に計算する（.w の素の detJ を使うと最小カスケードが常時飽和する）。
// カスケード間の相殺・強め合いは格子が異なるためここでは扱えない
// （Water.PS 側の瞬時項 ComputeFFTCombinedDetJ が補完する）。
// ============================================================

Texture2DArray<float4> gJacobian : register(t0);
Texture2DArray<float> gFoamPrev : register(t1);
// 出力は全スライスを 1 ビューで見せる UAV（Dispatch z = カスケード）
RWTexture2DArray<float> gFoamOutput : register(u0);

// C++ 側 FFTOceanManager::FoamConstants とレイアウト一致必須
cbuffer FFTOceanFoamConstants : register(b0)
{
    uint gResolution;
    float gDeltaSeconds;        // 前フレームからのシミュレーション経過時間 [s]
    float gFoamBias;            // 発生しきい値（WaterFrameConstants と同値）
    float gFoamGain;            // 立ち上がり勾配
    float3 gFoamCascadeWeights; // カスケード別の勾配寄与（無重みは31mカスケードが飽和）
    float gFoamDecaySeconds;    // 泡の寿命 τ（e^-1 に減衰するまでの秒数）
    uint gResetFoam;            // 1 = 前フレームを無視して初期化（生成直後・設定変更後）
    float3 gFoamPadding;
};

// 蓄積（持続泡）へのカスケード別の参加率。
// 持続する白波は「うねり＝大波の砕波」の現象で、さざ波の泡は瞬時に消える。
// 特に最小カスケード（31m）は単体 detJ のレンジが極端（勾配∝k で ±数倍）なため、
// 勾配重み 0.2 の緩和後でもしきい値を割る領域が広く、さらに波周期が約 4.5 秒と
// 短くて頻繁に再注入されるので、蓄積に参加させると海面全体が白く飽和する（実測）。
// 小カスケードの砕波は Water.PS の瞬時項（合成 detJ）が既に表現している。
static const float kAccumulationShare[3] = { 1.0f, 0.4f, 0.0f };

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const uint slice = min(dispatchThreadId.z, 2u);
    const float w = gFoamCascadeWeights[slice];

    // 重み付き detJ = det(I + w·J)。勾配テンソルへ重みを掛けてから行列式を取る
    // （Water.PS の ComputeFFTCombinedDetJ と同じ流儀。detJ の線形補間より正確）。
    const float3 j = gJacobian.Load(int4(dispatchThreadId.xy, slice, 0)).xyz * w;
    const float detJ = (1.0f + j.x) * (1.0f + j.y) - j.z * j.z;

    // 注入は二乗特性で「弱い圧縮」を非線形に抑圧する。カスケード単体の圧縮域は
    // （合成 detJ と違い他カスケードの伸長による相殺が働かないため）空間的に広く、
    // 波峰が参照格子上を位相速度で掃引しながら毎フレーム注入するので、線形のまま
    // 蓄積すると数周期で海面全体が泡の包絡に埋まり真っ白に飽和する（実測）。
    // 二乗にすると「強く砕けた峰」だけが持続泡として残り、弱い圧縮は跡を残さない。
    const float breaking = saturate((gFoamBias - detJ) * gFoamGain);
    const float injection = breaking * breaking * kAccumulationShare[slice];

    const float prev = (gResetFoam != 0)
        ? 0.0f
        : gFoamPrev.Load(int4(dispatchThreadId.xy, slice, 0));

    // 指数減衰。dt=0（ポーズ中）は decay=1 で凍結される。
    const float decay = exp(-gDeltaSeconds / max(gFoamDecaySeconds, 1.0e-3f));

    gFoamOutput[dispatchThreadId] = max(prev * decay, injection);
}
