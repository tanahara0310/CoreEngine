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

/// サンライトマーチのステップ数
static const int kCloudSunMarchSteps = 6;

/// サンライトマーチの歩幅の伸長率と、6 歩の等比級数の和（1+2+4+8+16+32）
static const float kCloudSunMarchStepGrowth = 2.0f;
static const float kCloudSunMarchStepSum = 63.0f;

/// 経路長を求めるときの太陽高度 sin の下限。低い太陽で斜め経路が発散するのを止める
static const float kCloudMinSunElevationSin = 0.15f;

/// 経路長の上限（層厚の倍数）。上限が無いと低い太陽で 1 歩目が粗くなりすぎる
static const float kCloudSunMaxPathMultiple = 3.0f;

/// サンライトマーチのコーンサンプル方向。全画素で同じ決定的なカーネルなので
/// 画面空間の模様を作らない（ここを乱数にすると斜めドット格子が雲面に焼き付く）
static const float3 kCloudSunConeKernel[6] = {
    float3( 0.38051305f,  0.92453449f, -0.02111345f),
    float3(-0.50625799f, -0.03590792f, -0.86163418f),
    float3(-0.32509218f, -0.94557439f,  0.01428793f),
    float3( 0.09026238f, -0.27376545f,  0.95755165f),
    float3( 0.28128598f,  0.42443639f, -0.86065785f),
    float3(-0.16852403f,  0.14748697f,  0.97460106f),
};

/// 多重散乱のオクターブ数（Hillaire）
static const int kCloudMultiScatterOctaves = 3;

/// 等方位相関数の値。オクターブを等方へ寄せる先
static const float kCloudIsotropicPhase = 0.0795774715f; // 1 / (4π)

/// 地面反射光が雲層内を上へ届く範囲を絞るべき指数。大きいほど雲底だけに効く
static const float kCloudGroundReachPower = 4.0f;

// ===== ノイズの LOD =====

/// ノイズテクスチャの一辺のテクセル数（C++ CloudResources の k*Size と一致させること）
static const float kCloudBaseNoiseTexels = 128.0f;
static const float kCloudDetailNoiseTexels = 32.0f;

/// 参照できる最上位ミップ段（C++ kNoiseMipLevels - 1 と一致させること）
static const float kCloudMaxNoiseLod = 4.0f;

// ===== ボクセルトラバーサル（ブロック雲）=====

/// レイに平行な軸で 0 除算にならないようにする方向成分の下限
static const float kCloudVoxelMinDirComponent = 1e-6f;

/// 単位ベクトルの L1 ノルムの上限（体対角 = √3）
/// @details DDA が 1m あたりにまたぐ境界の数は |方向| の L1 ノルム / セル幅。
///          軸沿い 1 〜 体対角 √3 で 1.7 倍変わるので、到達距離を方向ごとに算出すると
///          同じ距離の雲が方位によって出たり消えたりする。最も不利な向きで揃えること。
static const float kCloudVoxelMaxL1 = 1.7320508f;

/// ボクセルマーチの反復予算を MaxSteps の何倍にするか
/// @details 予算が MaxMarchDistance より手前で尽きると遠方の雲がフェードで消える。
///          DDA の 1 反復は「セル中心で密度を 1 回引く」だけで、体積マーチのような
///          サンライトマーチ（コーン 6 サンプル × 多重散乱 3 オクターブ）を伴わないため、
///          同じ予算でもコストは桁違いに軽い。遠方まで届かせるほうが得。
static const uint kCloudVoxelBudgetScale = 8u;

/// 面の明るさとコントラスト（voxelFaceBrightness / voxelFaceShadeMin）は
/// 実物合わせが要るので CVar にしてある（r.Cloud.VoxelFaceBrightness / VoxelFaceShadeMin）。

// ===== 巻雲シェル =====

/// 粗い筋を割る細かいノイズの周波数倍率と、そのしきい値
static const float kCirrusFineFrequency = 4.3f;
static const float kCirrusFineCutoff = 0.35f;

/// 視線が殻を斜めに貫く長さの倍率。分母の下限と倍率の上限
static const float kCirrusMinSlantCos = 0.12f;
static const float kCirrusMaxSlant = 8.0f;

/// 氷晶の前方散乱の強さ
static const float kCirrusPhaseG = 0.7f;

/// 巻雲へ届く環境光の倍率。薄い層なので下層より弱くする
static const float kCirrusAmbientScale = 0.12f;

// ===== アップサンプル =====

/// これ以上の NDC 深度は不透明物なし（遠クリップ＝空）とみなす
static const float kCloudDepthFarThreshold = 0.9999999f;

/// 不透明物が無い画素の距離。有限の距離との差が必ず棄却域に入る大きさであること
static const float kCloudNoOpaqueDistance = 1e8f;

/// 重み総和がこれを下回ったら加重平均を諦め、最も深度の近いタップをそのまま使う
static const float kCloudUpsampleMinWeight = 1e-4f;

#endif // CLOUD_TUNING_HLSLI
