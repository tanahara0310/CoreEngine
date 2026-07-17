#pragma once

#include "Graphics/Water/Surface/WaterDebugViewMode.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

#include <cstdint>

static constexpr uint32_t kMaxWaterWaveCount = 16;
static constexpr uint32_t kWaterWaveLayerCount = 3;

enum class WaterWaveLayerType : uint32_t {
	Large = 0,
	Medium = 1,
	Small = 2,
};

struct WaterWaveLayerConfig {
	uint32_t count = 0;
	float amplitudeMin = 0.0f;
	float amplitudeMax = 0.0f;
	float wavelengthMin = 1.0f;
	float wavelengthMax = 1.0f;
	float speedMin = 0.0f;
	float speedMax = 0.0f;
	float steepnessMin = 0.0f;
	float steepnessMax = 0.0f;
	float directionSpreadRadians = 0.0f;
};

/// @brief Gerstner Wave 1 本分のパラメータ
/// @note HLSL 側の WaveParams 構造体とメモリレイアウトを一致させること
struct WaveParams {
	CoreEngine::Vector2 direction = { 1.0f, 0.0f };
	float amplitude = 0.5f;
	float wavelength = 10.0f;
	float speed = 2.0f;
	float steepness = 0.5f;
	float phaseOffset = 0.0f;
	float padding = 0.0f;
};

/// @brief 水面 Gerstner Wave の定数バッファ全体
/// @note HLSL 側の WaterConstants cbuffer とメモリレイアウトを一致させること
struct WaterConstants {
	WaveParams waves[kMaxWaterWaveCount];
	uint32_t activeWaveCount = kMaxWaterWaveCount;
	float time = 0.0f;
	float padding[2] = {};

	static_assert(sizeof(WaveParams) == 32, "WaveParams must be 32 bytes");
};

/// @brief 毎フレーム更新する水面用フレーム定数バッファ
/// @note HLSL 側の WaterFrameConstants と一致させること
struct WaterFrameConstants {
	float clipPlane[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
	int clipEnabled = 0;
	int reflectionEnabled = 0;
	float fresnelReflectanceScale = 1.0f;
	float fresnelBaseReflectance = 0.02f;
	int depthFadeEnabled = 1;
	int depthFadeDebugEnabled = 0;
	float depthFadeDebugScale = 1.5f;
	// 空アンビエントの輝度単位 → サーフェス光単位の変換係数（AtmosphereManager::GetSkyAmbientScale と同値）
	float skyAmbientScale = 0.3f;
	// 波長依存の吸収係数 σa [1/m]（RGB）。赤 > 緑 > 青 が水の青さの物理的源泉
	float absorptionCoeff[3] = { 0.35f, 0.07f, 0.02f };
	// 1 = 大気散乱の Sky Irradiance SH を天空光として使う（大気アクティブ＋SH生成済みのシーンのみ）
	int skyAmbientEnabled = 0;
	// 波長依存の散乱係数 σs [1/m]（RGB）。深瀬のインスキャッタ色 (σs/σt) を決める
	float scatteringCoeff[3] = { 0.003f, 0.008f, 0.016f };
	float scatteringPad = 0.0f;
	uint32_t depthDebugViewMode = static_cast<uint32_t>(WaterDebugViewMode::None);
	// FFT Ocean 使用時、頂点解像度に依存しないピクセル単位の法線マップ再サンプリングを行うか
	int useFFTOceanNormalMap = 0;
	// 大気散乱の空気遠近感を水面へ適用するか（大気アクティブなシーンでのみ 1）
	int aerialPerspectiveEnabled = 0;
	// 空スペキュラキューブマップで平面反射へ雲を合成するか（大気アクティブ＋生成済みのみ 1）
	int skyEnvReflectionEnabled = 0;
};

enum class WaterPresetType : int {
	Lake = 0,
	Ocean = 1,
	Pool = 2,
	Rain = 3,
};

struct WaterPresetData {
	const char* name;
	uint32_t recommendedWaveCount;
	CoreEngine::Vector4 baseColor;
	float roughness;
	float metallic;
	// 波長依存の光学特性（水質）。色は光源 × 係数から導出されるため色指定は持たない
	CoreEngine::Vector3 absorptionCoeff; // σa [1/m]
	CoreEngine::Vector3 scatteringCoeff; // σs [1/m]
	float fresnelReflectanceScale;
	float fresnelBaseReflectance;
	CoreEngine::Vector2 dominantDirection;
	float directionJitterRadians;
	WaterWaveLayerConfig layers[kWaterWaveLayerCount];
	CoreEngine::Vector2 scrollSpeed;
	CoreEngine::Vector2 uvTiling;
};

inline const WaterPresetData& GetWaterPresetData(WaterPresetType type) {
	static const WaterPresetData kLake = {
		"湖（静かな水）",
		16,
		{ 0.04f, 0.18f, 0.28f, 0.85f },
		0.03f,
		0.0f,
		// やや緑がかった淡水: 溶存有機物で青の吸収が増え、緑寄りの散乱が強い
		{ 0.45f, 0.09f, 0.06f },
		{ 0.010f, 0.032f, 0.028f },
		1.0f, 0.02f,
		{ 1.0f, 0.0f },
		1.4f,
		{
			{ 3, 0.060f, 0.105f, 12.0f, 18.0f, 1.1f, 1.7f, 0.12f, 0.26f, 0.35f },
			{ 5, 0.026f, 0.052f,  7.0f, 12.0f, 1.5f, 2.2f, 0.06f, 0.16f, 0.70f },
			{ 8, 0.008f, 0.022f,  4.5f,  8.0f, 2.0f, 2.9f, 0.03f, 0.09f, 1.20f },
		},
		{ 0.03f, 0.01f },
		{ 4.0f, 4.0f },
	};

	static const WaterPresetData kOcean = {
		"海（波が立つ水）",
		16,
		{ 0.03f, 0.12f, 0.22f, 0.88f },
		0.10f,
		0.0f,
		// 澄んだ熱帯外洋水（Jerlov I 相当）: 浅瀬エメラルド〜深瀬青が σ から生じる
		{ 0.35f, 0.07f, 0.02f },
		{ 0.003f, 0.008f, 0.016f },
		1.0f, 0.02f,
		{ 0.95f, 0.25f },
		1.9f,
		{
			{ 4, 0.145f, 0.255f, 14.0f, 24.0f, 2.2f, 3.1f, 0.14f, 0.30f, 0.50f },
			{ 5, 0.062f, 0.118f,  8.0f, 14.0f, 2.9f, 3.8f, 0.07f, 0.18f, 0.90f },
			{ 7, 0.024f, 0.048f,  4.0f,  8.0f, 3.8f, 5.2f, 0.03f, 0.09f, 1.40f },
		},
		{ 0.06f, 0.03f },
		{ 3.0f, 3.0f },
	};

	static const WaterPresetData kPool = {
		"池・プール",
		16,
		{ 0.02f, 0.22f, 0.35f, 0.78f },
		0.01f,
		0.0f,
		// 純水に近い透明度: 吸収・散乱とも最小で、シアン寄りの淡い色になる
		{ 0.30f, 0.05f, 0.015f },
		{ 0.002f, 0.004f, 0.007f },
		1.0f, 0.02f,
		{ 1.0f, 0.1f },
		1.0f,
		{
			{ 2, 0.020f, 0.038f, 10.0f, 14.0f, 0.8f, 1.1f, 0.05f, 0.12f, 0.18f },
			{ 4, 0.008f, 0.018f,  6.5f, 10.0f, 1.0f, 1.4f, 0.03f, 0.07f, 0.45f },
			{ 10, 0.003f, 0.008f, 3.5f,  6.5f, 1.1f, 1.6f, 0.01f, 0.04f, 0.90f },
		},
		{ 0.02f, 0.008f },
		{ 5.0f, 5.0f },
	};

	static const WaterPresetData kRain = {
		"雨水・水たまり",
		16,
		{ 0.06f, 0.10f, 0.15f, 0.72f },
		0.06f,
		0.0f,
		// 濁水: 泥・有機物が短波長（青）を強く吸収し、散乱も強く茶褐色寄りになる
		{ 0.60f, 0.70f, 1.10f },
		{ 0.35f, 0.30f, 0.22f },
		1.0f, 0.02f,
		{ 0.8f, 0.2f },
		2.2f,
		{
			{ 1, 0.013f, 0.022f, 6.0f, 8.0f, 2.4f, 3.1f, 0.04f, 0.07f, 0.20f },
			{ 4, 0.010f, 0.018f, 3.0f, 5.5f, 3.3f, 4.4f, 0.03f, 0.06f, 0.80f },
			{ 11, 0.005f, 0.012f, 1.2f, 3.0f, 4.4f, 7.2f, 0.01f, 0.04f, 1.80f },
		},
		{ 0.04f, 0.02f },
		{ 6.0f, 6.0f },
	};

	static const WaterPresetData* kTable[] = { &kLake, &kOcean, &kPool, &kRain };
	const int idx = static_cast<int>(type);
	return *kTable[(idx >= 0 && idx < 4) ? idx : 0];
}
