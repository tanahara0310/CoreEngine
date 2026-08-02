// ============================================================
// 水面フレーム定数バッファ（b5）の唯一の HLSL 宣言
// ------------------------------------------------------------
// 以前は Water.PS / Water.VS / FFTWater.VS の 3 本に同じ cbuffer が
// 手書き複製されており、VS 側 2 本はフィールド名まで食い違っていた
// （PS の gAerialPerspectiveEnabled / gSkyEnvReflectionEnabled の位置が
// VS では float2 gDebugPadding。サイズが偶然一致していただけ）。
// フィールドの追加・変更は必ずこのファイルと C++ 側
// WaterSurfaceTypes.h の WaterFrameConstants（static_assert 群つき）の
// 2 箇所だけを同時に編集すること。
// ============================================================
#ifndef WATER_FRAME_CONSTANTS_INCLUDED
#define WATER_FRAME_CONSTANTS_INCLUDED

// WaterPlaneObject::BindCustomResources() が b5 にバインドする。
// C++ 側の対応構造体: WaterFrameConstants（WaterSurfaceTypes.h）
cbuffer WaterFrameConstants : register(b5)
{
    int gReflectionEnabled; // 1 = 反射テクスチャ有効，0 = IBL フォールバック
    float gFresnelReflectanceScale; // Fresnel 反射率スケール
    float gFresnelBaseReflectance; // 正面入射時の反射率（F0）

    // ---- Depth Fade ----
    int gDepthFadeEnabled; // 1 = Depth Fade 有効
    int gDepthFadeDebugEnabled; // 1 = 水深デバッグ表示
    float gDepthFadeDebugScale; // 水深デバッグ表示倍率
    // 空アンビエントの輝度単位 → サーフェス光単位の変換係数（AtmosphereManager::GetSkyAmbientScale と同値）
    float gSkyAmbientScale;
    // 1 = 大気散乱の Sky Irradiance SH を天空光として使う（大気アクティブ＋SH生成済みのシーンのみ）
    int gSkyAmbientEnabled;

    // ---- 水の光学特性（波長依存 Beer-Lambert）----
    // 水の色は shallow/deep の色指定ではなく、吸収・散乱係数と光源から導出する。
    // 赤 > 緑 > 青 の順に吸収が強いことが「水が青い」物理の本体。
    float3 gAbsorptionCoeff; // 吸収係数 σa [1/m]（RGB 波長別）
    float gAbsorptionPad;
    float3 gScatteringCoeff; // 散乱係数 σs [1/m]（RGB 波長別）
    float gScatteringPad;

    // ---- デバッグ表示 ----
    uint gDepthDebugViewMode;
    // FFT Ocean 使用時、頂点解像度に依存しないピクセル単位の法線マップ再サンプリングを行うか
    int gUseFFTOceanNormalMap;
    // 大気散乱の空気遠近感を水面へ適用するか（大気アクティブなシーンでのみ 1）
    int gAerialPerspectiveEnabled;
    // 空スペキュラキューブマップで平面反射へ雲を合成するか（大気アクティブ＋生成済みのみ 1）
    int gSkyEnvReflectionEnabled;

    // ---- 描画カメラのクリップ距離（LinearizeDepth 用）----
    // C++ 側 WaterFrameConstants::cameraNearZ / cameraFarZ と一致させること。
    // ここをハードコードしてはいけない（エディタ保存カメラは far=100000 で、
    // 既定 1000 と食い違うと水柱厚さが数十%狂う）。
    float gCameraNearZ;
    float gCameraFarZ;
    float2 gCameraClipPadding;

    // ---- 泡（whitecap）。FFTOcean 専用（Gerstner はヤコビアンを持たない）----
    int gFoamEnabled;      // 1 = 泡合成を行う
    float gFoamBias;       // 発生しきい値（合成 detJ がこれ未満で泡）
    float gFoamGain;       // しきい値からの立ち上がり勾配
    float gFoamOpacity;    // 泡レイヤの不透明度（1.0 の白ベタは禁止・水面下の情報を残す）
    float3 gFoamCascadeWeights; // カスケード別の勾配寄与（無重みは31mカスケードが支配して飽和する）
    // 泡の寿命 τ [s]。PS では未使用（FFTOceanFoamAccumulate.CS が使う）。
    // WaterRenderFeature が毎フレーム FFTOceanManager::SetFoamSettings へ転送する。
    float gFoamDecaySeconds;
};

#endif // WATER_FRAME_CONSTANTS_INCLUDED
