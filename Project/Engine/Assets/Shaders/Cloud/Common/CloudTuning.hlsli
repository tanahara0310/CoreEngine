/// @file CloudTuning.hlsli
/// @brief 雲のレイマーチ・ライティングで使う固定値
/// @details CVar に出していない美術値をここへ集約する。
///          チューニング指針は Docs/Engine/Graphics/Cloud/VolumetricCloud_Refactoring_Plan.md。

#ifndef CLOUD_TUNING_HLSLI
#define CLOUD_TUNING_HLSLI

// ===== 密度サンプル =====

/// 高度スキュー量 [m]。高い位置ほど風下へずらして雲を斜めに立たせる
static const float kCloudHeightSkewM = 500.0f;

/// ベースノイズの縦方向スケール倍率。薄い層に縦の変化を持たせるため水平より細かくサンプルする
static const float kCloudBaseNoiseVerticalScale = 0.5f;

// ===== マーチング =====

/// ステップ幅の基準となる層厚の分割数
static const float kCloudBaseStepDivisor = 128.0f;

/// ステップ幅の下限 [m]
static const float kCloudMinStepM = 4.0f;

/// 雲を探す大股走査のステップ倍率
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

/// ディテール侵食を距離で弱めきるまでの距離 [m]
static const float kCloudDetailFadeDistanceM = 25000.0f;

/// マーチ最大距離の手前で密度をフェードさせる幅 [m]
static const float kCloudFarFadeWidthM = 8000.0f;

// ===== ライティング =====

/// サンライトマーチの光学的深さの上限。飽和させると暗部の階調が消えるためクランプする
static const float kCloudMaxSunOpticalDepth = 8.0f;

/// サンライトマーチのステップ数（1 歩ごとに 2 倍へ伸びる）
static const int kCloudSunMarchSteps = 6;

/// 多重散乱のオクターブ数（Hillaire）
static const int kCloudMultiScatterOctaves = 3;

/// アンビエントで Sky-View LUT を引く仰角の cos 値（半球平均の代用）
static const float kCloudAmbientCosZenith = 0.7f;

/// 雲底の空遮蔽率。層の底ほど空からの光が届かない
static const float kCloudAmbientBottomOcclusion = 0.45f;

/// アンビエントに残す彩度。雲内部は多重散乱で無彩色に寄るため大部分を灰色へ寄せる
static const float kCloudAmbientChroma = 0.35f;

// ===== 空気遠近 =====

/// 遠方の雲が空色へ溶けるまでの消散距離 [m]
static const float kCloudHazeDistanceM = 60000.0f;

#endif // CLOUD_TUNING_HLSLI
