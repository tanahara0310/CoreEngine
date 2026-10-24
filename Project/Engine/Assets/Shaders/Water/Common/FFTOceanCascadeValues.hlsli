// ============================================================
// FFT Ocean カスケードの数値定義（C++ / HLSL 共通の唯一の情報源）
// ------------------------------------------------------------
// このファイルはプリプロセッサマクロだけで構成されており、HLSL と C++ の
// どちらからも #include できる。値を変える場合はここだけを書き換えること。
//
// 参照元:
//   HLSL … Water/Common/FFTOceanCascade.hlsli（型付き定数へ展開）
//   C++  … Engine/Src/Graphics/Water/FFTOceanManager.cpp
//
// 以前はこれらの数値が FFTWater.VS / Water.PS / RTWaterSurfaceCommon / FFTOceanManager
// の 4 箇所に手コピーされ、「一致必須」というコメントだけで守られていた。
// 片方だけ変えると RT が評価する波面とラスタ描画の波面がズレ、屈折の depth mismatch や
// 水中判定の帯として現れる（過去に実際に起きた事故）。
// ============================================================
#ifndef FFT_OCEAN_CASCADE_VALUES_INCLUDED
#define FFT_OCEAN_CASCADE_VALUES_INCLUDED

// カスケード数。異なるパッチ長の独立 FFT を合算し、単一タイルの「格子状の繰り返し」を打ち消す。
#define FFT_OCEAN_CASCADE_COUNT 3

// 各カスケードのワールドパッチ長（m）。すべて素数にして互いに素とし、
// 合成波形の繰り返し周期を LCM = 521×127×31 ≈ 205万m まで引き延ばす。
#define FFT_OCEAN_CASCADE0_PATCH_LENGTH 521.0f
#define FFT_OCEAN_CASCADE_PATCH_LENGTHS { FFT_OCEAN_CASCADE0_PATCH_LENGTH, 127.0f, 31.0f }

// カスケードごとのサンプリング格子回転（cos/sin）。0° / +26° / -49°。
// 全カスケードの格子軸が揃っていると、各カスケード固有のタイル周期が同じ向き・
// 同じ位置で強め合い「格子状の同じパターン」として知覚される。格子を互いに
// 回転させると残存周期が空間的に整列しなくなり視認できなくなる。
// スペクトルの風向は逆回転で補正するため、波の進行方向はワールドで全カスケード共通のまま。
#define FFT_OCEAN_CASCADE_ROT_COS { 1.0f, 0.89879405f, 0.65605903f }
#define FFT_OCEAN_CASCADE_ROT_SIN { 0.0f, 0.43837115f, -0.75471006f }

// ============================================================
// カスケードの担当波数帯（帯域制限）
// ------------------------------------------------------------
// 各カスケードは「自分の帯だけ」を持つ。以前は 3 カスケードすべてが
// 同じ完全な Phillips スペクトルを生成しており（パッチ長だけ違う）、
//   ・波長 4〜31m が 3 重、4〜127m が 2 重に計上される
//   ・カスケード1/2 は Phillips のピーク波数が自分の k_min より下にあるため
//     エネルギーが「自分のタイルの基本波」に積み上がる
//     ＝支配波長がタイル長と一致し、繰り返しが最も目立つ形になる
//   ・合成後のスペクトル形状を決めているのが手調整の配分比だけになる
// という 3 つの問題があった。格子回転と波群エンベロープはその対症療法。
//
// 境界は「波長（m）」で定義する。C++ 側が k = 2π/λ へ変換して使う。
// 相補的な窓（パワーの分割）で滑らかに受け渡すため、和は常に 1 になる。
#define FFT_OCEAN_BAND_LAMBDA_01 60.0f   // カスケード0 と 1 の境界
#define FFT_OCEAN_BAND_LAMBDA_12 12.0f   // カスケード1 と 2 の境界
// 境界のクロスフェード幅（ln k 上の半値幅）。2×これが境界間の ln 比
// （ln(60/12)=1.61）より小さいこと＝遷移が重ならないこと。
#define FFT_OCEAN_BAND_LOG_HALFWIDTH 0.35f

// 各カスケードの帯でいちばん長い波長 [m]。カスケード0 はパッチ長、ほかは 1 つ長い側の帯の境界。
#define FFT_OCEAN_CASCADE_LONGEST_WAVELENGTHS { FFT_OCEAN_CASCADE0_PATCH_LENGTH, FFT_OCEAN_BAND_LAMBDA_01, FFT_OCEAN_BAND_LAMBDA_12 }

// 頂点変位（ジオメトリ）にカスケードを入れる重み。
// 格子の頂点の間隔で、カスケードの帯でいちばん長い波長を 1 波長あたり何頂点で表せるかで決め、
// MIN 頂点で 0、FULL 頂点で 1 へなめらかに上げる（ComputeFFTGeometryCascadeWeight）。
// 粗い格子（15.6m 間隔）はカスケード0 だけ、岸の細かい格子（0.5m 間隔）は全カスケードになる。
#define FFT_OCEAN_GEOMETRY_MIN_VERTICES_PER_WAVE 4.0f
#define FFT_OCEAN_GEOMETRY_FULL_VERTICES_PER_WAVE 8.0f

// 波群エンベロープ（タイル周期破壊の空間振幅変調）の強さ。
// カスケード0（波高を支配する最大パッチ）は自分自身のタイルが 521m 周期で正確に
// 繰り返すため、「同じ波の高低差が一定間隔で並ぶ」列がどうしても残る。
// 無理数比の方向・波長（≈364/377/707m）の正弦和による滑らかな包絡を波高に掛けると、
// どのカスケード格子とも整列しない非周期変調になり、同一コピーが二度と同じ強さで
// 現れなくなる（実海面の波群＝セット波の見た目にも一致）。
#define FFT_OCEAN_WAVE_GROUP_STRENGTH 0.12f

// 波群エンベロープの 3 つの正弦の波数ベクトル（ワールド XZ）[rad/m] と初期位相 [rad]。
// 位相は波のエネルギーが群速度で進んだ分だけずらす（FFTOceanManager::ComputeWaveGroupPhase）。
#define FFT_OCEAN_WAVE_GROUP_WAVE_VECTOR_X { 0.01071f, -0.01409f, 0.00531f }
#define FFT_OCEAN_WAVE_GROUP_WAVE_VECTOR_Z { 0.01353f, 0.00893f, -0.00713f }
#define FFT_OCEAN_WAVE_GROUP_INITIAL_PHASE { 0.917f, 2.618f, 4.523f }

#endif // FFT_OCEAN_CASCADE_VALUES_INCLUDED
