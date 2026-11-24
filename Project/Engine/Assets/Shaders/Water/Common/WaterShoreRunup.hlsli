// ============================================================
// 岸の波（沖から寄せる波・砕けて岸へ進む段・浜を駆け上がって引く水の膜・砕けた波の泡）
// ------------------------------------------------------------
// 海の範囲マップ（WaterSeaMap.hlsli）の水深と、岸へ寄せる波が沖から着くまでの時間の場（SampleShoreWaveField）から、
// 水面の頂点を持ち上げる高さを返す。n 番目の波は、場所 x を時刻 (n 番目の波が沖を出る時刻 + 沖から x まで着く時間) に通る。
// 波は同じ番号どうしで、沖・浅瀬・水際がつながる。
//   - 沖から寄せる波: 水深 kShoreWaveMaxDepth より浅い所で、峰が着く時間の等しい線に沿って岸へ進む。
//                     浅くなるほど遅く（浜と平行に向きをそろえる）、高く（水深の -1/4 乗）、峰が細く、前の面が急になる。
//                     砕ける直前から、峰の上ほど前へずれて前の面が縦に近づき、そのまま崩れて段へ移る
//   - 砕けて進む段: 波の高さが水深の kWaterBreakingIndex 倍に届いた所から、段（ボア）になって岸へ進む
//   - 水際: 波が着くと、薄い水の膜が浜を駆け上がって引く。先端は波の高さから決めた速さで出て、坂と摩擦で減速して止まり、
//           止まった所から流れ下る。膜は先端より下ほど厚く、先端に丸い前の厚さが付き、引くにつれて薄くなる。
//           先端より先の水面は砂の下へ下げ、水面が浜の砂より高い所だけ水に見える。
//           勢いと上がり始める時刻は、水際に沿った位置の長さの違うノイズで波ごとに変わる（水際が弧の形に出入りする）。
//           ノイズと浜の傾きは頂点を水深 0 の線へ寄せた位置で引く（浜を上がる向きには同じ値になる）
//   - 波は 2 つの列で寄せる。主なうねりと、少し斜めから来る低いうねり（峰に沿って進むほど遅れて着く）
//   - 波ごとの沖を出る時刻と高さを、波を運んできた沖の点の峰に沿った位置で変える（峰が曲がり、ところどころ低く途切れる）。
//     主なうねりは、大きい波と小さい波が組になって来る。ばらつきは列ごとに決まった数の波でくり返す
//   - 波の高さは、ワールド XZ の位置でも波ごとに変える（同じ峰でも場所ごとに砕ける水深が変わる）
//   - 砕けた波の泡: 沖から寄せる波があるとき、砕ける直前の峰の頂・段の前の面が通った直後（時間とともに減る）・
//                   浜を上がる膜の先端の細い帯と、上がり始めの膜に出す。段の泡の出始めの遅れ・減る早さ・量の上限は、ワールド XZ のノイズで変える
//   - 濡れ跡: 浜の 1 点から膜が引いてからの時間で、砂の暗さ（ゆっくり乾く）とつや（すぐ消える）を返す（EvaluateSwashWetness）
// 利用側: FFTWater.VS / RTWaterSurfaceCommon.hlsli（同じ式で形をそろえる）
// ============================================================
#ifndef WATER_SHORE_RUNUP_HLSLI
#define WATER_SHORE_RUNUP_HLSLI

#include "WaterSeaMap.hlsli"

/// @brief 1 回の遡上が上がって引くまでの長さ（波の間隔に対する割合）
static const float kRunupDurationRatio = 0.8f;
/// @brief 膜の先端が駆け上がる時間の上限（1 回の遡上の長さに対する割合。これより長く上がる浜は摩擦で止める）
static const float kRunupPeakRatio = 0.35f;
/// @brief 浜を駆け上がる勢いの高さの、平均に対する上限の倍率（√kShoreWaveMaxHeightScale × (1 + kRunupReachVariation) 以上。WaterSystem.cpp と同じ値）
static const float kRunupMaxHeightScale = 2.2f;
/// @brief 水際の届く高さと上がり始める時刻を水際に沿って変えるノイズの数と、それぞれの長さ [m] と重み（重みの和は 1）
static const int kRunupEdgeOctaves = 2;
static const float kRunupEdgeNoiseMeters[2] = { 20.0f, 9.0f };
static const float kRunupEdgeNoiseWeights[2] = { 0.65f, 0.35f };
/// @brief 水際のノイズの重み付きの和の標準偏差（これで割って ±2 で切り、2 で割って [-1, 1] にする）
static const float kRunupEdgeNoiseStdDev = 0.159f;
/// @brief 浜を駆け上がる勢いの高さのばらつき（平均に対する ± の割合）
static const float kRunupReachVariation = 0.3f;
/// @brief 水際が上がり始める時刻のばらつき（1 回の遡上の長さに対する ± の割合）
static const float kRunupEdgeDelayVariation = 0.1f;
/// @brief 水際のノイズを引く位置を水深 0 の線へ寄せるときの、浜の傾きの下限
static const float kRunupEdgeMinSlope = 0.02f;
/// @brief 水際のノイズを引く位置を水深 0 の線へ寄せる距離の上限 [m]
static const float kRunupEdgeMaxShift = 40.0f;
/// @brief 水際のノイズの乱数の列の始まり（GetShoreWave の列と重ならない）
static const float kRunupEdgeHashRow = 128.0f;
/// @brief 段の高さを水深どおりにする浜の傾き（これより緩い浜ほど段を低くする）
static const float kRunupReferenceSlope = 0.05f;
/// @brief 膜の水際での厚さを沖へ延ばす水深 [m]
static const float kRunupLensDepth = 0.8f;
/// @brief 膜の厚さ ÷ 先端からの高さの差（上がる間と、引き終わり）
static const float2 kSwashThicknessRate = float2(0.12f, 0.03f);
/// @brief 膜の先端の丸い前の厚さ [m]（上がる間と、引き終わり）
static const float2 kSwashFrontThickness = float2(0.05f, 0.015f);
/// @brief 膜の先端の前の厚さが付く長さ・先端より先で水面を砂の下へ下げる長さ [m]（水平）
static const float kSwashFrontLength = 0.6f;
/// @brief 膜の先端より先で、水面を砂の下へ下げる深さ [m]
static const float kSwashAheadDepth = 0.06f;
/// @brief 膜の先端より先の乾いた所で、水面を止める高さ（先端の高さから）[m]
static const float kSwashDryCap = 0.08f;
/// @brief 膜の先端の泡の量（上がる間と、引き終わり）
static const float2 kSwashTipFoam = float2(0.95f, 0.35f);
/// @brief 膜の先端の泡が付く長さ [m]（水平）
static const float kSwashTipFoamLength = 0.5f;
/// @brief 濡れ跡の砂の暗さとつやが、水が引いてから 1/e に減るまでの時間 [s]
static const float kSwashWetDarkSeconds = 25.0f;
static const float kSwashWetGlossSeconds = 3.0f;
/// @brief 濡れ跡で見る、今の波より前の波の数
static const int kSwashWetPastWaves = 6;
/// @brief 濡れ跡を求める水深の上限 [m]
static const float kSwashWetMaxDepth = 0.5f;
/// @brief 浜を上がる膜が水の粒を運ぶずれを、水際から沖へ 0 まで弱める長さ [m]（水平）
static const float kSwashFlowFadeMeters = 16.0f;
/// @brief 前の膜が引いている所へ来た段の前の面を高くする倍率（引く水の厚さが kBackwashBoreReference 以上のとき）
static const float kBackwashBoreGain = 0.8f;
/// @brief 前の膜が引いている所へ来た段の泡を増やす倍率（引く水の厚さが kBackwashBoreReference 以上のとき）
static const float kBackwashBoreFoamGain = 0.6f;
/// @brief 段を高くするのに使う、引く水の厚さの基準 [m]
static const float kBackwashBoreReference = 0.03f;
/// @brief 段の前の面が立ち上がってから、引く水で高くした分を戻し終えるまでの時間 [s]
static const float kBackwashBoreFrontSeconds = 2.0f;
/// @brief 砕けた波が段になって進む水深の上限 [m]（沖から寄せる波が無いとき）
static const float kRunupBoreDepth = 1.6f;
/// @brief 砕けた波が段になって進む水深の上限 [m]（沖から寄せる波があるとき。砕けた波だけが段になる）
static const float kRunupBoreDepthWithShoreWaves = 3.0f;
/// @brief 段の高さ ÷ 水深
static const float kRunupBoreHeightRatio = 0.4f;
/// @brief 段の前の面が立ち上がる時間 [s]
static const float kRunupBoreRiseSeconds = 1.2f;
/// @brief 水深の傾きを測る間隔 [m]
static const float kRunupGradientStep = 3.0f;
/// @brief 傾きの下限
static const float kRunupMinSlope = 0.01f;
/// @brief 砕けた波の泡が、段の前の面が通ってから満量になるまでの時間 [s]
static const float kShoreBreakFoamRiseSeconds = 0.25f;
/// @brief 砕けた波の泡が、段の前の面が通ってから 1/e に減るまでの時間 [s]
static const float kShoreBreakFoamDecaySeconds = 1.8f;
/// @brief 砕けた波の泡の量 ÷ 波の高さの倍率（1 で頭打ち）
static const float kShoreBreakFoamHeightGain = 1.6f;
/// @brief 砕けた波の泡の、段の前の面が通った直後の量（kShoreBreakFoamMax を超えた分は頭打ち）
static const float kShoreBreakFoamFront = 1.6f;
/// @brief 砕けた波の泡の量の上限（場所ごとに、この間で変える）
static const float2 kShoreBreakFoamMax = float2(0.55f, 0.9f);
/// @brief 砕けた波の泡の出始めの遅れと、減る早さ・量の上限を場所で変えるノイズの長さ [m]
static const float2 kShoreBreakFoamNoiseMeters = float2(9.0f, 6.0f);
/// @brief 砕けた波の泡が、段の前の面が通ってから出始めるまでの遅れの上限 [s]（場所ごとに 0 からこの値まで）
static const float kShoreBreakFoamDelaySeconds = 0.7f;
/// @brief 砕けた波の泡が 1/e に減るまでの時間の倍率の幅（場所ごとに、この間で変える）
static const float2 kShoreBreakFoamDecayScale = float2(0.45f, 1.55f);
/// @brief 砕けた波の泡のノイズの乱数の列の始まり（GetShoreWave・GetRunupEdge・GetShoreWaveLocalScale の列と重ならない）
static const float kShoreBreakFoamHashRow = 448.0f;
/// @brief 砕ける直前の峰の頂の泡が出始める・出そろう、波の高さ ÷ 砕ける高さ
static const float2 kShoreCrestFoamRatio = float2(0.9f, 1.1f);
/// @brief 砕ける直前の峰の頂の泡の量
static const float kShoreCrestFoamAmount = 0.8f;
/// @brief 波の高さをワールドの位置で変えるノイズの長さ [m]
static const float kShoreWaveLocalNoiseMeters = 26.0f;
/// @brief 波の高さをワールドの位置で変える倍率の最小と最大
static const float2 kShoreWaveLocalScale = float2(0.5f, 1.0f);
/// @brief 波の高さをワールドの位置で変えるノイズの値がこの間で、倍率が最小から最大へ移る
static const float2 kShoreWaveLocalNoiseRange = float2(0.2f, 0.5f);
/// @brief 波の高さをワールドの位置で変えるノイズの乱数の列の始まり（GetShoreWave・GetRunupEdge の列と重ならない）
static const float kShoreWaveLocalHashRow = 320.0f;
/// @brief 浜を上がる膜の泡の量（上がり始めと、減り終わり）
static const float kShoreSwashFoamStart = 0.5f;
static const float kShoreSwashFoamEnd = 0.05f;
/// @brief 浜を上がる膜の泡が減り終わるまでの時間（1 回の遡上の長さに対する割合）
static const float kShoreSwashFoamFadeRatio = 0.5f;
/// @brief 重力加速度 [m/s²]
static const float kRunupGravity = 9.81f;
/// @brief 2π
static const float kRunupTwoPi = 6.2831853f;

/// @brief 沖から寄せる波を描く水深の上限 [m]（これより深い所は FFT の波だけ）
static const float kShoreWaveMaxDepth = 12.0f;
/// @brief 沖から寄せる波をそのままの高さで描く水深の上限 [m]
static const float kShoreWaveFullDepth = 7.0f;
/// @brief 峰の幅（周期に対する割合。水深 kShoreWaveMaxDepth と、砕ける直前）
static const float kShoreWaveCrestWidthOffshore = 0.15f;
static const float kShoreWaveCrestWidthBreaking = 0.07f;
/// @brief 砕ける直前の峰の前後の幅の偏り（前の面を細く・後ろの面を広く）
static const float kShoreWaveCrestSkew = 0.45f;
/// @brief 波の高さ ÷ (水深 × kWaterBreakingIndex) がこの間で、寄せる波から段へ移る
static const float kShoreWaveBreakStartRatio = 0.85f;
static const float kShoreWaveBreakEndRatio = 1.15f;
/// @brief 峰の幅を、頂点の格子の間隔の何倍より細くしないか
static const float kShoreWaveMinCrestWidthSpacings = 1.5f;
/// @brief 波の高さ ÷ (水深 × kWaterBreakingIndex) がこの値から 1 までの間で、峰が前へ倒れ始める
static const float kShoreWaveLeanStartRatio = 0.7f;
/// @brief 峰の頂を前へずらす長さ ÷ 峰の高さ
static const float kShoreWaveLeanHeightRatio = 1.2f;
/// @brief 前の面を縮める割合の下限（前へずらした後の前の面の長さ ÷ 元の長さ。0 で面が縦に立つ）
static const float kShoreWaveMinFrontStretch = 0.12f;
/// @brief 峰の頂を L だけ前へずらしたとき、前の面がいちばん縮む所の縮み方 ÷ (L ÷ 前の面の幅)（2 / √e）
static const float kShoreWaveLeanCompression = 1.2130613f;
/// @brief 峰が通ってからの時間がこの値 × 波の間隔より離れた頂点はずらさない
static const float kShoreWaveLeanReachPeriods = 0.6f;
/// @brief √π
static const float kShoreWaveSqrtPi = 1.7724539f;

/// @brief 峰に沿った高さの倍率（ノイズの値が kShoreWaveSectionLowNoise 以下で最小・kShoreWaveSectionHighNoise 以上で最大）
static const float kShoreWaveSectionMinScale = 0.12f;
static const float kShoreWaveSectionMaxScale = 1.5f;
static const float kShoreWaveSectionLowNoise = 0.3f;
static const float kShoreWaveSectionHighNoise = 0.7f;
/// @brief 波ごとの高さの倍率の幅
static const float kShoreWavePerWaveMinScale = 0.85f;
static const float kShoreWavePerWaveMaxScale = 1.15f;
/// @brief 主なうねりの、大きい波と小さい波の組の高さの倍率（kShoreWaveMainCycle 個の波の並び）
static const float kShoreWaveSetEnvelope[16] = {
    0.6f, 0.75f, 1.0f, 1.3f, 1.4f, 1.15f, 0.85f, 0.7f, 0.8f, 1.0f, 1.2f, 1.05f, 0.8f, 0.65f, 0.6f, 0.7f };
/// @brief 高さの倍率の上限（kShoreWaveSetEnvelope の最大 × kShoreWavePerWaveMaxScale × kShoreWaveSectionMaxScale。WaterSystem.cpp と同じ値）
static const float kShoreWaveMaxHeightScale = 2.42f;

/// @brief 岸へ寄せる波の列の数（0 = 主なうねり・1 = 少し斜めから来る低いうねり）
static const int kShoreWaveTrainCount = 2;
/// @brief 主なうねりの、波ごとのばらつきがくり返す波の数（間隔 × この数 = kShoreWaveFieldCycleSeconds）
static const int kShoreWaveMainCycle = 16;
/// @brief 斜めから来るうねりの、波ごとのばらつきがくり返す波の数（間隔 × この数 = kShoreWaveFieldCycleSeconds）
static const int kShoreWaveCrossCycle = 13;

/// @brief 岸へ寄せる波の列
struct ShoreWaveTrain
{
    float periodSeconds;  ///< 波の間隔 [s]
    int cycle;            ///< 波ごとのばらつきがくり返す波の数
    float delayPerMeter;  ///< 峰に沿った位置が 1m 進むごとに遅れて着く時間 [s/m]
    float heightRatio;    ///< 高さの倍率
    float sectionMeters;  ///< 着く時刻と高さを変える、峰に沿った長さ [m]
    float arrivalJitter;  ///< 着く時刻のばらつき（間隔に対する割合・±半分）
    float hashRow;        ///< 乱数の列の始まり
};

/// @brief 岸へ寄せる波の列 train の決まり
ShoreWaveTrain GetShoreWaveTrain(int train)
{
    ShoreWaveTrain w;
    if (train == 0)
    {
        w.periodSeconds = kShoreWaveFieldCycleSeconds / (float)kShoreWaveMainCycle;
        w.cycle = kShoreWaveMainCycle;
        w.delayPerMeter = 0.0f;
        w.heightRatio = 1.0f;
        w.sectionMeters = 32.0f;
        w.arrivalJitter = 0.35f;
        w.hashRow = 0.0f;
    }
    else
    {
        w.periodSeconds = kShoreWaveFieldCycleSeconds / (float)kShoreWaveCrossCycle;
        w.cycle = kShoreWaveCrossCycle;
        w.delayPerMeter = 0.03f;
        w.heightRatio = 0.65f;
        w.sectionMeters = 28.0f;
        w.arrivalJitter = 0.3f;
        w.hashRow = 64.0f;
    }
    return w;
}

/// @brief 2 次元の整数格子の点の乱数 [0, 1)
float RunupHash(float2 p)
{
    const float3 q = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    const float3 r = q + dot(q, q.yzx + 33.33f);
    return frac((r.x + r.y) * r.z);
}

/// @brief 乱数の列 row の、1 次元のなめらかな値のノイズ [0, 1)
float RunupValueNoise(float x, float row)
{
    const float cell = floor(x);
    const float f = x - cell;
    const float u = f * f * (3.0f - 2.0f * f);
    return lerp(RunupHash(float2(cell, row)), RunupHash(float2(cell + 1.0f, row)), u);
}

/// @brief 波の列 train の n 番目の波の、峰に沿った位置での沖を出る時刻 [s] と高さの倍率
/// @param crestMeters 波を運んできた沖の点の峰に沿った位置 [m]（SampleShoreWaveField の w）
void GetShoreWave(int train, int waveIndex, float crestMeters, out float departureSeconds, out float heightScale)
{
    const ShoreWaveTrain w = GetShoreWaveTrain(train);
    const int cycleIndex = ((waveIndex % w.cycle) + w.cycle) % w.cycle;
    const float row = w.hashRow + (float)cycleIndex;
    const float x = crestMeters / w.sectionMeters;

    const float jitter = w.arrivalJitter * (RunupValueNoise(x, row) - 0.5f);
    departureSeconds = ((float)waveIndex + jitter) * w.periodSeconds;

    const float section = lerp(kShoreWaveSectionMinScale, kShoreWaveSectionMaxScale,
        smoothstep(kShoreWaveSectionLowNoise, kShoreWaveSectionHighNoise, RunupValueNoise(x + 0.5f, row + 32.0f)));
    const float perWave = lerp(kShoreWavePerWaveMinScale, kShoreWavePerWaveMaxScale, RunupHash(float2(row, 211.0f)));
    const float set = (train == 0) ? kShoreWaveSetEnvelope[cycleIndex] : 1.0f;
    heightScale = w.heightRatio * set * perWave * section;
}

/// @brief 乱数の列 row の、2 次元のなめらかな値のノイズ [0, 1)
float RunupValueNoise2D(float2 p, float row)
{
    const float2 cell = floor(p);
    const float2 f = p - cell;
    const float2 u = f * f * (3.0f - 2.0f * f);
    const float2 c = cell + float2(row * 41.0f, row * 29.0f);
    const float c00 = RunupHash(c);
    const float c10 = RunupHash(c + float2(1.0f, 0.0f));
    const float c01 = RunupHash(c + float2(0.0f, 1.0f));
    const float c11 = RunupHash(c + float2(1.0f, 1.0f));
    return lerp(lerp(c00, c10, u.x), lerp(c01, c11, u.x), u.y);
}

/// @brief 波の列 train の n 番目の波の、ワールド XZ での高さの倍率（同じ峰でも場所ごとに砕ける水深が変わる）
float GetShoreWaveLocalScale(int train, int waveIndex, float2 worldXZ)
{
    const ShoreWaveTrain w = GetShoreWaveTrain(train);
    const int cycleIndex = ((waveIndex % w.cycle) + w.cycle) % w.cycle;
    const float row = kShoreWaveLocalHashRow + w.hashRow + (float)cycleIndex;
    const float noise = RunupValueNoise2D(worldXZ / kShoreWaveLocalNoiseMeters, row);
    return lerp(kShoreWaveLocalScale.x, kShoreWaveLocalScale.y,
        smoothstep(kShoreWaveLocalNoiseRange.x, kShoreWaveLocalNoiseRange.y, noise));
}

/// @brief 格子の点 cell の、乱数で決めた向きと、点からのずれ offset の内積
float RunupGradientCorner(float2 cell, float2 offset)
{
    float s;
    float c;
    sincos(kRunupTwoPi * RunupHash(cell), s, c);
    return c * offset.x + s * offset.y;
}

/// @brief 乱数の列 row の、2 次元のなめらかな傾きのノイズ（格子の点で 0。およそ [-0.7, 0.7]）
float RunupGradientNoise2D(float2 p, float row)
{
    const float2 cell = floor(p);
    const float2 f = p - cell;
    const float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const float2 c = cell + float2(row * 41.0f, row * 29.0f);
    const float n00 = RunupGradientCorner(c, f);
    const float n10 = RunupGradientCorner(c + float2(1.0f, 0.0f), f - float2(1.0f, 0.0f));
    const float n01 = RunupGradientCorner(c + float2(0.0f, 1.0f), f - float2(0.0f, 1.0f));
    const float n11 = RunupGradientCorner(c + float2(1.0f, 1.0f), f - float2(1.0f, 1.0f));
    return lerp(lerp(n00, n10, u.x), lerp(n01, n11, u.x), u.y);
}

/// @brief ワールド XZ での、長さの違うノイズの重み付きの和を [-1, 1] にした値
float RunupEdgeNoise(float2 worldXZ, float row)
{
    float sum = 0.0f;
    [unroll]
    for (int i = 0; i < kRunupEdgeOctaves; ++i)
    {
        sum += kRunupEdgeNoiseWeights[i] * RunupGradientNoise2D(worldXZ / kRunupEdgeNoiseMeters[i], row + 16.0f * (float)i);
    }
    return 0.5f * clamp(sum / kRunupEdgeNoiseStdDev, -2.0f, 2.0f);
}

/// @brief 頂点を水深 0 の線へ寄せたワールド XZ（水深の傾きの向きへ、水深 ÷ 傾き だけ動かす）
/// @param depth     頂点の、今の水面からの水深 [m]（陸の上は負）
/// @param gradient  水深の傾き（∂水深/∂x, ∂水深/∂z）
float2 ComputeRunupEdgePosition(float2 worldXZ, float depth, float2 gradient)
{
    const float gradientLength = max(length(gradient), kRunupEdgeMinSlope);
    const float shift = clamp(-depth / gradientLength, -kRunupEdgeMaxShift, kRunupEdgeMaxShift);
    return worldXZ + shift * gradient / gradientLength;
}

/// @brief 波の列 train の n 番目の波が水際を上がるときの、水際の位置での高さの倍率と遅れ
/// @param edgeXZ      頂点を水深 0 の線へ寄せたワールド XZ（ComputeRunupEdgePosition）
/// @param reachScale  水際の届く高さの倍率
/// @param delayRatio  上がり始める時刻の遅れ（1 回の遡上の長さに対する割合）
void GetRunupEdge(int train, int waveIndex, float2 edgeXZ, out float reachScale, out float delayRatio)
{
    const ShoreWaveTrain w = GetShoreWaveTrain(train);
    const int cycleIndex = ((waveIndex % w.cycle) + w.cycle) % w.cycle;
    const float row = kRunupEdgeHashRow + w.hashRow + (float)cycleIndex;
    reachScale = 1.0f + kRunupReachVariation * RunupEdgeNoise(edgeXZ, row);
    delayRatio = kRunupEdgeDelayVariation * RunupEdgeNoise(edgeXZ, row + 48.0f);
}

/// @brief 沖から寄せる波の、水深 depth での高さ ÷ (水深 × kWaterBreakingIndex)（1 を超えると砕けている）
float ComputeShoreWaveBreakingRatio(float shoreWaveHeight, float heightScale, float depth)
{
    const float safeDepth = max(depth, 0.01f);
    const float shoaledHeight = shoreWaveHeight * heightScale * pow(kShoreWaveMaxDepth / safeDepth, 0.25f);
    return shoaledHeight / (kWaterBreakingIndex * safeDepth);
}

/// @brief 浜を上がる膜の先端が止まるまでの時間と、止まる所の高さ
/// @details 先端は高さ launchHeight まで投げ上げる速さで出て、坂（重力 × 傾き）で減速する。
///          止まるまでの時間が kRunupPeakRatio × duration を超える浜は、その時間で止まるよう減速を強める
/// @param upSeconds    上がり始めてから止まるまでの時間 [s]
/// @param reachHeight  止まる所の高さ [m]（静水面から）
void ComputeSwashTipReach(float launchHeight, float beachSlope, float duration, out float upSeconds, out float reachHeight)
{
    const float launchSpeed = sqrt(2.0f * kRunupGravity * launchHeight);
    upSeconds = min(launchSpeed / (kRunupGravity * beachSlope), kRunupPeakRatio * duration);
    reachHeight = 0.5f * launchSpeed * upSeconds * beachSlope;
}

/// @brief 浜を上がる膜の先端の高さ [m]（静水面から。上がっていないときは 0）
/// @details 先端は ComputeSwashTipReach の時間で止まる所まで上がり、止まった所から、残りの時間で静水面まで流れ下る
/// @param launchHeight  勢いの高さ [m]（急な浜で届く高さ）
/// @param beachSlope    水際の浜の傾き
/// @param duration      1 回の遡上の長さ [s]
/// @param ageSeconds    上がり始めてからの時間 [s]
/// @param backwash      引いている割合 [0,1]（上がる間は 0）
float EvaluateSwashTip(float launchHeight, float beachSlope, float duration, float ageSeconds, out float backwash)
{
    backwash = 0.0f;
    if (ageSeconds <= 0.0f || ageSeconds >= duration || launchHeight <= 0.0f)
    {
        return 0.0f;
    }
    float upSeconds;
    float reachHeight;
    ComputeSwashTipReach(launchHeight, beachSlope, duration, upSeconds, reachHeight);
    if (ageSeconds < upSeconds)
    {
        const float x = (upSeconds - ageSeconds) / upSeconds;
        return reachHeight * (1.0f - x * x);
    }
    backwash = (ageSeconds - upSeconds) / (duration - upSeconds);
    return reachHeight * (1.0f - backwash * backwash);
}

/// @brief 浜を上がる膜の水面の高さ [m]（静水面から）
/// @details 膜の厚さは、先端からの高さの差に比例する分と、先端の丸い前の厚さの和。
///          先端より先は水面を砂の下へ下げ、乾いた所は先端の高さ + kSwashDryCap で止める。
///          海の中は、静水面の水際での膜の厚さを水深 kRunupLensDepth までで 0 へ下げる
/// @param elevation   砂の高さ [m]（静水面から。海の中は負）
/// @param tipHeight   先端の高さ [m]（EvaluateSwashTip）
/// @param backwash    引いている割合 [0,1]
/// @param beachSlope  水際の浜の傾き
float EvaluateSwashSheet(float elevation, float tipHeight, float backwash, float beachSlope)
{
    const float rate = lerp(kSwashThicknessRate.x, kSwashThicknessRate.y, backwash);
    const float front = lerp(kSwashFrontThickness.x, kSwashFrontThickness.y, backwash);
    if (elevation >= 0.0f)
    {
        const float below = tipHeight - elevation;
        const float edge = tanh(below / (beachSlope * kSwashFrontLength));
        const float thickness = rate * below + ((below >= 0.0f) ? front : kSwashAheadDepth) * edge;
        return min(elevation + thickness, tipHeight + kSwashDryCap);
    }
    const float shorelineThickness = rate * tipHeight + front * tanh(tipHeight / (beachSlope * kSwashFrontLength));
    return shorelineThickness * saturate(1.0f + elevation / kRunupLensDepth);
}

/// @brief 浜の 1 点が 1 回の遡上で膜に覆われ、膜が引いてからの時間 [s]
/// @details 先端（EvaluateSwashTip）が上がるときにこの点の高さを通ってから、引くときに通るまでは覆われている（0 を返す）。
///          先端が届かない点と、先端がまだ来ていない点は負を返す
/// @param elevation 砂の高さ [m]（静水面から。海の中は 0 として扱う）
float EvaluateSwashUncoveredSeconds(float launchHeight, float beachSlope, float duration, float ageSeconds, float elevation)
{
    if (ageSeconds <= 0.0f || launchHeight <= 0.0f)
    {
        return -1.0f;
    }
    float upSeconds;
    float reachHeight;
    ComputeSwashTipReach(launchHeight, beachSlope, duration, upSeconds, reachHeight);
    const float height = max(elevation, 0.0f);
    if (height >= reachHeight)
    {
        return -1.0f;
    }
    // 先端の高さ reachHeight × (1 − x²) がこの点の高さになる x
    const float x = sqrt(1.0f - height / reachHeight);
    const float arriveSeconds = upSeconds * (1.0f - x);
    const float leaveSeconds = upSeconds + (duration - upSeconds) * x;
    if (ageSeconds < arriveSeconds)
    {
        return -1.0f;
    }
    return max(ageSeconds - leaveSeconds, 0.0f);
}

/// @brief 段（ボア）の形（0..1。前の面で立ち上がり、次の波までに下がる）
/// @param ageSeconds 段の前の面が通ってからの時間 [s]
/// @param lifetime   段が消えるまでの時間 [s]
float RunupBoreShape(float ageSeconds, float lifetime)
{
    if (ageSeconds <= 0.0f || ageSeconds >= lifetime)
    {
        return 0.0f;
    }
    return smoothstep(0.0f, kRunupBoreRiseSeconds, ageSeconds) * (1.0f - ageSeconds / lifetime);
}

/// @brief 峰の頂がいる所の値から決める、峰の頂を前へずらす長さと、ずらす形の前後の幅
/// @param crestSeaMap   峰の頂がいる所の海の範囲マップの値（SampleWaterSeaMap）
/// @param depthOffset   今の水面の高さ − 測ったときの水面の高さ [m]
/// @param heightScale   波の高さの倍率
/// @param periodSeconds 波の間隔 [s]
/// @param vertexSpacing 頂点の格子の間隔 [m]
/// @return (峰の頂を前へずらす長さ [m], 前の幅 [m], 後ろの幅 [m], その所で波が 1m 進むのにかかる時間 [s/m])。ずらさないときは x = 0
float4 EvaluateShoreWaveCrestLean(
    float4 crestSeaMap, float depthOffset, float shoreWaveHeight, float heightScale, float periodSeconds, float vertexSpacing)
{
    if (IsWaterSeaMapBasin(crestSeaMap.x))
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    const float depth = crestSeaMap.x + depthOffset;
    if (depth <= 0.0f || depth >= kShoreWaveMaxDepth)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    const float breakingRatio = ComputeShoreWaveBreakingRatio(shoreWaveHeight, heightScale, depth);
    const float leanWeight = smoothstep(kShoreWaveLeanStartRatio, 1.0f, breakingRatio);
    if (leanWeight <= 0.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    const float unbroken = 1.0f - smoothstep(kShoreWaveBreakStartRatio, kShoreWaveBreakEndRatio, breakingRatio);
    const float offshoreWeight = 1.0f - smoothstep(kShoreWaveFullDepth, kShoreWaveMaxDepth, depth);
    const float crestHeight = min(breakingRatio, 1.0f) * kWaterBreakingIndex * depth * unbroken * offshoreWeight;

    // 峰の幅は EvaluateShoreWave と同じ式（秒をその所の波の速さで長さにする）
    const float speed = sqrt(kRunupGravity * depth);
    const float steepness = saturate(breakingRatio);
    const float widthMeters = max(
        periodSeconds * lerp(kShoreWaveCrestWidthOffshore, kShoreWaveCrestWidthBreaking, steepness) * speed,
        kShoreWaveMinCrestWidthSpacings * vertexSpacing);
    const float frontMeters = widthMeters * (1.0f - kShoreWaveCrestSkew * steepness);
    const float backMeters = widthMeters * (1.0f + kShoreWaveCrestSkew * steepness);
    const float leanMeters = min(kShoreWaveLeanHeightRatio * crestHeight * leanWeight,
        (1.0f - kShoreWaveMinFrontStretch) * frontMeters / kShoreWaveLeanCompression);
    return float4(leanMeters, frontMeters, backMeters, 1.0f / speed);
}

/// @brief 沖から寄せる波（砕ける前）の、静水面からの高さと傾きと水平のずれ
/// @details 砕ける直前から、峰の上ほど前（波の進む向き）へずらす。前の面は上ほど急に立ち、峰が崩れるまで縦に近づく。
///          ずらす長さと形の幅は峰の頂がいる所の値で決める（峰の前後の頂点が同じ形を使い、面が折れ重ならない）。
///          傾きは、ずらした後の頂点の位置での値
/// @param seaMap          海の範囲マップ
/// @param originXZ        地図の範囲の XZ の最小の角 [m]
/// @param texelsPerMeter  地図の 1m あたりのテクセル数
/// @param resolution      地図の X・Z のテクセル数
/// @param depthOffset     今の水面の高さ − 測ったときの水面の高さ [m]
/// @param depth         今の水面からの水深 [m]
/// @param clearance     今の水面から上の物までの高さ [m]（これより上へは上げない）
/// @param field         SampleShoreWaveField の値
/// @param timeSeconds   波の時刻 [s]
/// @param shoreWaveHeight   水深 kShoreWaveMaxDepth での波の高さの平均 [m]（0 なら寄せる波なし）
/// @param vertexSpacing 頂点の格子の間隔 [m]
/// @param worldXZ       頂点の変位前のワールド XZ [m]
/// @param crestFoam     砕ける直前から峰の頂に出る泡の被覆率 [0,1]
/// @param horizontalOffset 頂点を前へずらす水平の変位 [m]
/// @return (高さ [m], ∂高さ/∂x, ∂高さ/∂z)
float3 EvaluateShoreWave(
    Texture2D<float4> seaMap, float2 originXZ, float texelsPerMeter, uint2 resolution, float depthOffset,
    float depth, float clearance, float4 field, float timeSeconds, float shoreWaveHeight, float vertexSpacing,
    float2 worldXZ, out float crestFoam, out float2 horizontalOffset)
{
    crestFoam = 0.0f;
    horizontalOffset = float2(0.0f, 0.0f);
    if (shoreWaveHeight <= 0.0f || depth <= 0.0f || depth >= kShoreWaveMaxDepth)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float offshoreSeconds = field.x;
    const float offshoreWeight = 1.0f - smoothstep(kShoreWaveFullDepth, kShoreWaveMaxDepth, depth);
    const float speed = sqrt(kRunupGravity * depth);
    const float minWidthSeconds = kShoreWaveMinCrestWidthSpacings * vertexSpacing / speed;
    // 沖から着く時間の傾きの大きさ（波の進む向きに 1m 進むと着く時間が何秒遅れるか）と、波の進む向き
    const float travelSlowness = length(field.yz);
    const float2 travelDirection = (travelSlowness > 1.0e-6f) ? field.yz / travelSlowness : float2(0.0f, 0.0f);

    float height = 0.0f;
    float slopeAlongTravel = 0.0f;
    float meanHeight = 0.0f;
    float lean = 0.0f;
    float leanRate = 0.0f;
    [unroll]
    for (int train = 0; train < kShoreWaveTrainCount; ++train)
    {
        const ShoreWaveTrain w = GetShoreWaveTrain(train);
        const float arrivalSeconds = offshoreSeconds + w.delayPerMeter * field.w;
        const int wave = (int)floor((timeSeconds - arrivalSeconds) / w.periodSeconds);
        // 近い 3 つの波の峰を重ねる
        [unroll]
        for (int i = -1; i <= 1; ++i)
        {
            float departure;
            float heightScale;
            GetShoreWave(train, wave + i, field.w, departure, heightScale);
            heightScale *= GetShoreWaveLocalScale(train, wave + i, worldXZ);
            const float breakingRatio = ComputeShoreWaveBreakingRatio(shoreWaveHeight, heightScale, depth);
            const float unbroken = 1.0f - smoothstep(kShoreWaveBreakStartRatio, kShoreWaveBreakEndRatio, breakingRatio);
            const float crestHeight = min(breakingRatio, 1.0f) * kWaterBreakingIndex * depth * unbroken * offshoreWeight;

            // 砕ける直前ほど峰が細く、前の面（峰より先に通る面）が急になる
            const float steepness = saturate(breakingRatio);
            const float width = max(
                w.periodSeconds * lerp(kShoreWaveCrestWidthOffshore, kShoreWaveCrestWidthBreaking, steepness), minWidthSeconds);
            const float frontWidth = width * (1.0f - kShoreWaveCrestSkew * steepness);
            const float backWidth = width * (1.0f + kShoreWaveCrestSkew * steepness);
            const float age = timeSeconds - (departure + arrivalSeconds);
            const float sideWidth = (age < 0.0f) ? frontWidth : backWidth;
            const float x = age / sideWidth;
            const float shape = exp(-x * x);
            height += crestHeight * shape;
            slopeAlongTravel += crestHeight * shape * (-2.0f * x / sideWidth);
            meanHeight += crestHeight * 0.5f * (frontWidth + backWidth) * kShoreWaveSqrtPi / (3.0f * w.periodSeconds);

            // 砕ける直前から、峰の上ほど前へずらす。峰の頂は (峰が通ってからの時間 ÷ 着く時間の傾き) だけ沖にいる
            if (abs(age) < kShoreWaveLeanReachPeriods * w.periodSeconds && travelSlowness > 1.0e-6f)
            {
                const float2 crestXZ = worldXZ + travelDirection * (age / travelSlowness);
                const float4 crestLean = EvaluateShoreWaveCrestLean(
                    SampleWaterSeaMap(seaMap, crestXZ, originXZ, texelsPerMeter, resolution),
                    depthOffset, shoreWaveHeight, heightScale, w.periodSeconds, vertexSpacing);
                if (crestLean.x > 0.0f)
                {
                    // 峰の頂から進む向きへの距離（着く時間の差 ÷ 頂点と峰の頂の、1m 進むのにかかる時間の平均）
                    const float aheadMeters = -age / (0.5f * (travelSlowness + crestLean.w));
                    const float leanWidth = (aheadMeters > 0.0f) ? crestLean.y : crestLean.z;
                    const float u = aheadMeters / leanWidth;
                    const float leanShape = exp(-2.0f * u * u);
                    lean += crestLean.x * leanShape;
                    leanRate += crestLean.x * leanShape * (-4.0f * u / leanWidth);
                }
            }

            // 砕ける直前から、峰の頂に泡を出す
            const float crestTip = shape * shape * shape * shape;
            crestFoam = max(crestFoam, kShoreCrestFoamAmount
                * smoothstep(kShoreCrestFoamRatio.x, kShoreCrestFoamRatio.y, breakingRatio) * crestTip * offshoreWeight);
        }
    }
    // 1 周期の平均が静水面になるよう、峰の分の平均を引く
    height -= meanHeight;

    const float ceiling = max(clearance - 0.05f, 0.0f);
    if (height > ceiling)
    {
        return float3(ceiling, 0.0f, 0.0f);
    }
    horizontalOffset = lean * travelDirection;
    // 峰が通ってからの時間は (時刻 − 沖から着く時間) で進むので、場所での傾きは沖から着く時間の傾きの逆向き。
    // 前へずらすと進む向きの長さが (1 + leanRate) 倍になるので、進む向きの傾きをその分だけ急にする
    const float stretch = max(1.0f + leanRate, kShoreWaveMinFrontStretch);
    return float3(height, -slopeAlongTravel * field.yz / stretch);
}

/// @brief 1 つの波の列で見る、近い波の数（2 つ前から 1 つ後まで）
static const int kRunupNearWaveCount = 4;
/// @brief 遡上を評価する波の数（全部の波の列の、近い波）
static const int kRunupCandidateCount = kShoreWaveTrainCount * kRunupNearWaveCount;

/// @brief 遡上を評価する場所の値（海の範囲マップから読む）
struct ShoreRunupSite
{
    float depth;      ///< 今の水面からの水深 [m]（陸の上は負）
    float clearance;  ///< 今の水面から上の物までの高さ [m]
    float slope;      ///< 水深の傾きの大きさ（kRunupMinSlope〜1）
    float2 upslope;   ///< 浜を上る向き（水深が浅くなる向き。長さ 1、傾きが無いときは 0）
    float2 edgeXZ;    ///< 頂点を水深 0 の線へ寄せたワールド XZ（ComputeRunupEdgePosition）
    float beachSlope; ///< 水際の浜の傾き（kRunupMinSlope〜1。同じ浜の断面の頂点が同じ値を使う）
    float4 field;     ///< 沖から着くまでの時間の場（SampleShoreWaveField。陸の上は、いちばん近い水際の値）
};

/// @brief 遡上を評価する場所の値を海の範囲マップから読む
/// @param maxDepth 評価する水深の上限 [m]
/// @return 囲まれた水・水深 maxDepth より深い所・勢いの高さの上限の 2 倍より高い陸は false
bool PrepareShoreRunupSite(
    Texture2D<float4> seaMap, float2 worldXZ, float2 originXZ, float texelsPerMeter, uint2 resolution,
    float depthOffset, float meanHeight, float maxDepth, out ShoreRunupSite site)
{
    site = (ShoreRunupSite)0;
    const float4 center = SampleWaterSeaMap(seaMap, worldXZ, originXZ, texelsPerMeter, resolution);
    if (IsWaterSeaMapBasin(center.x))
    {
        return false;
    }
    site.depth = center.x + depthOffset;
    site.clearance = center.y - depthOffset;
    if (site.depth > maxDepth || site.depth < -2.0f * kRunupMaxHeightScale * meanHeight)
    {
        return false;
    }

    // 水深の傾き（浜の坂）
    const float step = kRunupGradientStep;
    const float2 gradient = float2(
        SampleWaterSeaMap(seaMap, worldXZ + float2(step, 0.0f), originXZ, texelsPerMeter, resolution).x
            - SampleWaterSeaMap(seaMap, worldXZ - float2(step, 0.0f), originXZ, texelsPerMeter, resolution).x,
        SampleWaterSeaMap(seaMap, worldXZ + float2(0.0f, step), originXZ, texelsPerMeter, resolution).x
            - SampleWaterSeaMap(seaMap, worldXZ - float2(0.0f, step), originXZ, texelsPerMeter, resolution).x)
        / (2.0f * step);
    const float gradientLength = length(gradient);
    site.slope = clamp(gradientLength, kRunupMinSlope, 1.0f);
    site.upslope = (gradientLength > 1.0e-6f) ? -gradient / gradientLength : float2(0.0f, 0.0f);
    site.edgeXZ = ComputeRunupEdgePosition(worldXZ, site.depth, gradient);

    // 水際の浜の傾き
    const float2 edgeGradient = float2(
        SampleWaterSeaMap(seaMap, site.edgeXZ + float2(step, 0.0f), originXZ, texelsPerMeter, resolution).x
            - SampleWaterSeaMap(seaMap, site.edgeXZ - float2(step, 0.0f), originXZ, texelsPerMeter, resolution).x,
        SampleWaterSeaMap(seaMap, site.edgeXZ + float2(0.0f, step), originXZ, texelsPerMeter, resolution).x
            - SampleWaterSeaMap(seaMap, site.edgeXZ - float2(0.0f, step), originXZ, texelsPerMeter, resolution).x)
        / (2.0f * step);
    site.beachSlope = clamp(length(edgeGradient), kRunupMinSlope, 1.0f);

    site.field = SampleShoreWaveField(seaMap, worldXZ, originXZ, texelsPerMeter, resolution);
    return true;
}

/// @brief 岸の格子の頂点を持ち上げる高さ [m]（砕けて進む段と水際の上がり下がり）
/// @param seaMap          海の範囲マップ
/// @param worldXZ         頂点の変位前のワールド XZ [m]
/// @param originXZ        地図の範囲の XZ の最小の角 [m]
/// @param texelsPerMeter  地図の 1m あたりのテクセル数
/// @param resolution      地図の X・Z のテクセル数
/// @param depthOffset     今の水面の高さ − 測ったときの水面の高さ [m]
/// @param timeSeconds     波の時刻 [s]
/// @param meanHeight      浜を駆け上がる勢いの高さの平均 [m]（急な浜で届く高さ。0 なら持ち上げない）
/// @param shoreWaveHeight     沖から寄せる波の高さの平均 [m]（0 なら、どの波も水深 kRunupBoreDepth から段になる）
/// @param breakFoam       砕けた波の泡の被覆率 [0,1]（沖から寄せる波があるときだけ。段の前の面が通った直後に多く、時間とともに減る。
///                        浜を上がる膜は先端に多く、引くにつれて減る。段の泡の出始めの遅れ・減る早さ・量の上限は場所ごとに変わる）
/// @param flowShift       浜を上がる膜が運ぶ水の粒の水平のずれ [m]（浜を上る向き。陸の上は、いちばん上がっている膜の先端が
///                        静水面の水際から上がった長さ。沖へは水際から kSwashFlowFadeMeters で 0 へ弱める）
/// @details 前の波の膜が引いている所へ次の段が来ると、段の前の面を引く水の厚さの分だけ高くし、段の高さと泡も増やす
float EvaluateShoreRunupLift(
    Texture2D<float4> seaMap, float2 worldXZ, float2 originXZ, float texelsPerMeter, uint2 resolution,
    float depthOffset, float timeSeconds, float meanHeight, float shoreWaveHeight, out float breakFoam, out float2 flowShift)
{
    breakFoam = 0.0f;
    flowShift = float2(0.0f, 0.0f);
    if (meanHeight <= 0.0f || resolution.x == 0)
    {
        return 0.0f;
    }

    const bool withShoreWaves = shoreWaveHeight > 0.0f;
    const float boreDepth = withShoreWaves ? kRunupBoreDepthWithShoreWaves : kRunupBoreDepth;
    ShoreRunupSite site;
    if (!PrepareShoreRunupSite(seaMap, worldXZ, originXZ, texelsPerMeter, resolution, depthOffset, meanHeight, boreDepth, site))
    {
        return 0.0f;
    }
    const float depth = site.depth;
    const float clearance = site.clearance;
    const float slopeScale = saturate(site.slope / kRunupReferenceSlope);
    const float2 edgeXZ = site.edgeXZ;
    const float beachSlope = site.beachSlope;
    const float elevation = -depth;

    const float wetDepth = max(depth, 0.0f);
    const float4 field = site.field;
    const float offshoreSeconds = field.x;

    const float lensWeight = saturate(1.0f - wetDepth / kRunupLensDepth);
    const float boreDepthWeight = 1.0f - smoothstep(0.6f * boreDepth, boreDepth, wetDepth);
    const float boreWeight = boreDepthWeight * slopeScale;

    // 砕けた波の泡の、場所ごとの出始めの遅れ・減る早さ・量の上限
    float foamDelaySeconds = 0.0f;
    float foamDecaySeconds = kShoreBreakFoamDecaySeconds;
    float foamMax = kShoreBreakFoamMax.y;
    if (withShoreWaves)
    {
        const float delayNoise = saturate(0.5f + 0.7f
            * RunupGradientNoise2D(worldXZ / kShoreBreakFoamNoiseMeters.x, kShoreBreakFoamHashRow));
        const float amountNoise = RunupValueNoise2D(worldXZ / kShoreBreakFoamNoiseMeters.y, kShoreBreakFoamHashRow + 1.0f);
        foamDelaySeconds = kShoreBreakFoamDelaySeconds * delayNoise;
        foamDecaySeconds *= lerp(kShoreBreakFoamDecayScale.x, kShoreBreakFoamDecayScale.y, amountNoise);
        foamMax = lerp(kShoreBreakFoamMax.x, kShoreBreakFoamMax.y, amountNoise);
    }

    // 波ごとの段の高さ・前の面が通ってからの時間・消えるまでの時間・泡と、引いている膜の厚さ
    float boreHeights[kRunupCandidateCount];
    float boreAges[kRunupCandidateCount];
    float boreLifetimes[kRunupCandidateCount];
    float boreFoams[kRunupCandidateCount];
    float backwashDepths[kRunupCandidateCount];
    float lift = 0.0f;
    float foam = 0.0f;
    float tipMax = 0.0f;
    [unroll]
    for (int train = 0; train < kShoreWaveTrainCount; ++train)
    {
        const ShoreWaveTrain w = GetShoreWaveTrain(train);
        const float arrivalSeconds = offshoreSeconds + w.delayPerMeter * field.w;
        const float duration = kRunupDurationRatio * w.periodSeconds;
        const int wave = (int)floor((timeSeconds - arrivalSeconds) / w.periodSeconds);
        // 近い 4 つの波（2 つ前から 1 つ後まで）のうち、いちばん高い水面を使う
        [unroll]
        for (int n = 0; n < kRunupNearWaveCount; ++n)
        {
            const int k = train * kRunupNearWaveCount + n;
            const int waveIndex = wave + n - 2;
            float departure;
            float heightScale;
            GetShoreWave(train, waveIndex, field.w, departure, heightScale);
            const float age = timeSeconds - (departure + arrivalSeconds);

            // 浅瀬を進む段（沖から寄せる波があるときは、その場所で砕けている波だけ。高さは寄せる波と同じ場所ごとの倍率を掛ける）
            const float boreScale = withShoreWaves ? heightScale * GetShoreWaveLocalScale(train, waveIndex, worldXZ) : heightScale;
            const float broken = withShoreWaves
                ? smoothstep(kShoreWaveBreakStartRatio, kShoreWaveBreakEndRatio, ComputeShoreWaveBreakingRatio(shoreWaveHeight, boreScale, wetDepth))
                : 1.0f;
            boreHeights[k] = kRunupBoreHeightRatio * wetDepth * min(boreScale, 1.0f) * boreWeight * broken;
            boreAges[k] = age;
            boreLifetimes[k] = duration;

            // 浜を上がる膜（勢いの高さは波の高さの平方根で増え、水際に沿った位置と波ごとに変わる）
            float reachScale;
            float delayRatio;
            GetRunupEdge(train, waveIndex, edgeXZ, reachScale, delayRatio);
            const float swashAge = age - delayRatio * duration;
            float backwash;
            const float tipHeight = EvaluateSwashTip(
                meanHeight * sqrt(heightScale) * reachScale, beachSlope, duration, swashAge, backwash);
            backwashDepths[k] = 0.0f;
            if (tipHeight > 0.0f)
            {
                const float sheet = EvaluateSwashSheet(elevation, tipHeight, backwash, beachSlope);
                lift = max(lift, sheet);
                backwashDepths[k] = backwash * max(sheet - max(elevation, 0.0f), 0.0f);
                tipMax = max(tipMax, tipHeight);
            }

            boreFoams[k] = 0.0f;
            if (withShoreWaves)
            {
                // 砕けた波の泡（段の前の面が通った直後に多く、時間とともに減る）
                const float foamAge = age - foamDelaySeconds;
                boreFoams[k] = broken * boreDepthWeight * saturate(kShoreBreakFoamHeightGain * boreScale)
                    * min(kShoreBreakFoamFront * smoothstep(0.0f, kShoreBreakFoamRiseSeconds, foamAge)
                        * exp(-max(foamAge, 0.0f) / foamDecaySeconds), foamMax);
                // 浜を上がる膜の泡（先端の細い帯と、上がり始めの膜。どちらも引くにつれて減る）
                const float behindTip = (tipHeight - elevation) / beachSlope;
                if (tipHeight > 0.0f && behindTip >= 0.0f)
                {
                    const float tipRatio = behindTip / kSwashTipFoamLength;
                    const float tipFoam = lerp(kSwashTipFoam.x, kSwashTipFoam.y, backwash) * exp(-tipRatio * tipRatio);
                    const float sheetFoam = lerp(kShoreSwashFoamStart, kShoreSwashFoamEnd,
                            saturate(swashAge / (kShoreSwashFoamFadeRatio * duration)))
                        * ((elevation >= 0.0f) ? 1.0f : lensWeight);
                    foam = max(foam, max(tipFoam, sheetFoam));
                }
            }
        }
    }

    // 引いている膜のいちばん厚い波と、その厚さ・2 番目の厚さ
    int thickestBackwash = 0;
    float backwashFirst = 0.0f;
    float backwashSecond = 0.0f;
    [unroll]
    for (int b = 0; b < kRunupCandidateCount; ++b)
    {
        if (backwashDepths[b] > backwashFirst)
        {
            backwashSecond = backwashFirst;
            backwashFirst = backwashDepths[b];
            thickestBackwash = b;
        }
        else
        {
            backwashSecond = max(backwashSecond, backwashDepths[b]);
        }
    }

    // 段は、ほかの波の膜が引いている所では前の面をその厚さの分だけ高くし、高さと泡を増やす
    [unroll]
    for (int c = 0; c < kRunupCandidateCount; ++c)
    {
        const float backwashDepth = (c == thickestBackwash) ? backwashSecond : backwashFirst;
        const float front = 1.0f - smoothstep(
            kRunupBoreRiseSeconds, kRunupBoreRiseSeconds + kBackwashBoreFrontSeconds, boreAges[c]);
        const float collision = saturate(backwashDepth / kBackwashBoreReference) * front;
        const float ride = backwashDepth * front * saturate(boreHeights[c] / kBackwashBoreReference);
        lift = max(lift, (boreHeights[c] * (1.0f + kBackwashBoreGain * collision) + ride)
            * RunupBoreShape(boreAges[c], boreLifetimes[c]));
        foam = max(foam, boreFoams[c] * (1.0f + kBackwashBoreFoamGain * collision));
    }
    breakFoam = saturate(foam);

    // 浜を上がる膜が運ぶ水の粒のずれ（陸の上は一様、沖へは水際からの距離で弱め、段を評価する水深までに 0 にする）
    const float flowFade = saturate(1.0f - wetDepth / (site.slope * kSwashFlowFadeMeters)) * boreDepthWeight;
    flowShift = site.upslope * (tipMax / beachSlope) * flowFade;

    // 勢いの高さの上限と、桟橋の床などの上の物より上へは上げない
    return min(min(lift, kRunupMaxHeightScale * meanHeight), max(clearance - 0.05f, 0.0f));
}

/// @brief 岸の格子の頂点を持ち上げる高さ [m]（砕けた波の泡と水の粒のずれを使わない呼び出し用）
float EvaluateShoreRunupLift(
    Texture2D<float4> seaMap, float2 worldXZ, float2 originXZ, float texelsPerMeter, uint2 resolution,
    float depthOffset, float timeSeconds, float meanHeight, float shoreWaveHeight)
{
    float unusedFoam;
    float2 unusedFlow;
    return EvaluateShoreRunupLift(seaMap, worldXZ, originXZ, texelsPerMeter, resolution,
        depthOffset, timeSeconds, meanHeight, shoreWaveHeight, unusedFoam, unusedFlow);
}

/// @brief 浜を上がった膜の濡れ跡（x = 砂の暗さ [0,1]・y = 砂のつや [0,1]・z = 今の静水面からの砂の高さ [m]）
/// @details 波の列ごとに、今の波とその前の kSwashWetPastWaves 個の波で、この点から膜が引いてからの時間を求める。
///          暗さは kSwashWetDarkSeconds、つやは kSwashWetGlossSeconds で減らし、いちばん濡れている値を使う。
///          膜に覆われている間は 1。評価しない場所（PrepareShoreRunupSite が false）は 0
/// @param seaMap 〜 meanHeight  EvaluateShoreRunupLift と同じ
float3 EvaluateSwashWetness(
    Texture2D<float4> seaMap, float2 worldXZ, float2 originXZ, float texelsPerMeter, uint2 resolution,
    float depthOffset, float timeSeconds, float meanHeight)
{
    ShoreRunupSite site;
    if (meanHeight <= 0.0f || resolution.x == 0
        || !PrepareShoreRunupSite(seaMap, worldXZ, originXZ, texelsPerMeter, resolution,
            depthOffset, meanHeight, kSwashWetMaxDepth, site))
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float elevation = -site.depth;

    float2 wetness = float2(0.0f, 0.0f);
    [unroll]
    for (int train = 0; train < kShoreWaveTrainCount; ++train)
    {
        const ShoreWaveTrain w = GetShoreWaveTrain(train);
        const float arrivalSeconds = site.field.x + w.delayPerMeter * site.field.w;
        const float duration = kRunupDurationRatio * w.periodSeconds;
        const int wave = (int)floor((timeSeconds - arrivalSeconds) / w.periodSeconds);
        [loop]
        for (int i = -kSwashWetPastWaves; i <= 1; ++i)
        {
            float departure;
            float heightScale;
            GetShoreWave(train, wave + i, site.field.w, departure, heightScale);
            float reachScale;
            float delayRatio;
            GetRunupEdge(train, wave + i, site.edgeXZ, reachScale, delayRatio);
            const float swashAge = timeSeconds - (departure + arrivalSeconds) - delayRatio * duration;
            const float uncoveredSeconds = EvaluateSwashUncoveredSeconds(
                meanHeight * sqrt(heightScale) * reachScale, site.beachSlope, duration, swashAge, elevation);
            if (uncoveredSeconds >= 0.0f)
            {
                wetness = max(wetness, exp(-uncoveredSeconds / float2(kSwashWetDarkSeconds, kSwashWetGlossSeconds)));
            }
        }
    }
    return float3(wetness, elevation);
}

#endif // WATER_SHORE_RUNUP_HLSLI
