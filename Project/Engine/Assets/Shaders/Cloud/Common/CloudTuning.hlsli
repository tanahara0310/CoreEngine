/// @file CloudTuning.hlsli
/// @brief 雲のレイマーチで使う固定値
/// @details マーチの安定性やループ回数に関わる値だけを置く。
///          見た目のチューニング値は CVar（r.Cloud.*）から定数バッファ経由で来る。

#ifndef CLOUD_TUNING_HLSLI
#define CLOUD_TUNING_HLSLI

// ===== マーチング =====
// ステップ幅に関わる値はサンプル格子のワールド固定性を決める。
// 変えるとカメラ移動で雲面が上下に振動するため CVar には出さない。

/// ステップ幅の基準となる層厚の分割数
static const float kCloudBaseStepDivisor = 128.0f;

/// ステップ幅の下限 [m]
static const float kCloudMinStepM = 4.0f;

/// 雲を探す大股走査のステップ倍率。細ステップの整数倍であること
static const float kCloudCoarseStepScale = 4.0f;

/// 雲を抜けたと判断するまでの連続空サンプル数
static const int kCloudEmptyRunToExit = 8;

/// ステップ伸長の距離しきい値と伸長量（合計で最大 15 倍まで伸びる）
static const float kCloudStrideStart0M = 6000.0f;
static const float kCloudStrideRange0M = 24000.0f;
static const float kCloudStrideAmount0 = 5.0f;
static const float kCloudStrideStart1M = 30000.0f;
static const float kCloudStrideRange1M = 25000.0f;
static const float kCloudStrideAmount1 = 3.0f;
static const float kCloudStrideStart2M = 55000.0f;
static const float kCloudStrideRange2M = 45000.0f;
static const float kCloudStrideAmount2 = 7.0f;

// ===== ライティング =====
// ループ回数は [unroll] の対象なのでコンパイル時定数であること。

/// サンライトマーチのステップ数（1 歩ごとに 2 倍へ伸びる）
static const int kCloudSunMarchSteps = 6;

/// 多重散乱のオクターブ数（Hillaire）
static const int kCloudMultiScatterOctaves = 3;

#endif // CLOUD_TUNING_HLSLI
