#include "NoiseModule.h"
#include "../ParticleSystem.h" // Particle構造体のために必要
#include <cmath>

namespace CoreEngine
{

using namespace CoreEngine::MathCore;
NoiseModule::NoiseModule() {
	noiseData_.strength = 1.0f;
	noiseData_.frequency = 1.0f;
	noiseData_.scrollSpeed = 0.0f;
	noiseData_.damping = true;
	noiseData_.positionAmount = { 1.0f, 1.0f, 1.0f };
}

void NoiseModule::ApplyNoise(Particle& particle, float deltaTime) {
	if (!enabled_) {
		return;
	}

	// ライフタイムの進行度 (0.0 - 1.0)
	float lifeRatio = particle.currentTime / particle.lifeTime;

	// 減衰係数の計算
	float dampingFactor = 1.0f;
	if (noiseData_.damping) {
		// ライフタイムが進むにつれてノイズを減衰
		dampingFactor = 1.0f - lifeRatio;
	}

	// 時間ベースのオフセット（パーティクルの経過時間を使用）
	float timeOffset = particle.currentTime * noiseData_.scrollSpeed;

	// ノイズ計算用の座標（パーティクルの位置と時間を使用）
	Vector3 noiseCoord = {
		(particle.transform.translate.x + timeOffset) * noiseData_.frequency,
		(particle.transform.translate.y + timeOffset) * noiseData_.frequency,
	  (particle.transform.translate.z + particle.currentTime) * noiseData_.frequency
	};

	// 各軸のノイズを計算
	float noiseX = PerlinNoise3D(noiseCoord.x, noiseCoord.y, noiseCoord.z);
	float noiseY = PerlinNoise3D(noiseCoord.x + 100.0f, noiseCoord.y, noiseCoord.z);
	float noiseZ = PerlinNoise3D(noiseCoord.x, noiseCoord.y + 100.0f, noiseCoord.z);

	// ノイズによるオフセットを計算
	Vector3 noiseOffset = {
		noiseX * noiseData_.positionAmount.x * noiseData_.strength * dampingFactor,
		noiseY * noiseData_.positionAmount.y * noiseData_.strength * dampingFactor,
		noiseZ * noiseData_.positionAmount.z * noiseData_.strength * dampingFactor
	};

	// パーティクルの位置にノイズを適用
	particle.transform.translate.x += noiseOffset.x * deltaTime;
	particle.transform.translate.y += noiseOffset.y * deltaTime;
	particle.transform.translate.z += noiseOffset.z * deltaTime;
}

#ifdef _DEBUG
bool NoiseModule::ShowImGui() {
	bool changed = false;

	// 有効/無効の切り替え
	if (ImGui::Checkbox("有効##ノイズ", &enabled_)) {
		changed = true;
	}

	if (!enabled_) {
		ImGui::BeginDisabled();
	}

	ImGui::Text("パーリンノイズによる位置の揺らぎを追加します");
	ImGui::Separator();

	// 強度
	changed |= ImGui::DragFloat("強度", &noiseData_.strength, 0.1f, 0.0f, 10.0f);
	ImGui::TextDisabled("ノイズの影響の大きさ");

	// 周波数
	changed |= ImGui::DragFloat("周波数", &noiseData_.frequency, 0.01f, 0.01f, 5.0f);
	ImGui::TextDisabled("ノイズの細かさ（高いほど細かい揺らぎ）");

	// スクロール速度
	changed |= ImGui::DragFloat("スクロール速度", &noiseData_.scrollSpeed, 0.1f, -10.0f, 10.0f);
	ImGui::TextDisabled("ノイズパターンの移動速度");

	ImGui::Separator();

	// 各軸の影響量
	changed |= ImGui::DragFloat3("軸別影響量", &noiseData_.positionAmount.x, 0.1f, 0.0f, 5.0f);
	ImGui::TextDisabled("X, Y, Z軸それぞれのノイズ影響量");

	ImGui::Separator();

	// 減衰設定
	changed |= ImGui::Checkbox("ライフタイム減衰", &noiseData_.damping);
	ImGui::TextDisabled("ONにすると、パーティクルの寿命に応じてノイズが減衰します");

	if (!enabled_) {
		ImGui::EndDisabled();
	}

	return changed;
}
#endif

// ========================================
// パーリンノイズ実装
// ========================================

// Perlin Noiseの順列テーブル（Ken Perlinのオリジナル実装を参考）
static const int permutation[512] = {
	151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
	8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
	35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
 134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
	55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
	18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
 250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
	189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
  172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
	228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
	107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
	138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
	// 繰り返し
	151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
	8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
	35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
	134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
	55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
	18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
	250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
	189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
	172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
	228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
	107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
	138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

float NoiseModule::PerlinNoise3D(float x, float y, float z) {
	// 単位立方体の座標を計算
	int X = static_cast<int>(std::floor(x)) & 255;
	int Y = static_cast<int>(std::floor(y)) & 255;
	int Z = static_cast<int>(std::floor(z)) & 255;

	// 立方体内の相対座標
	x -= std::floor(x);
	y -= std::floor(y);
	z -= std::floor(z);

	// フェードカーブを計算
	float u = Fade(x);
	float v = Fade(y);
	float w = Fade(z);

	// ハッシュ座標計算
	int A = permutation[X] + Y;
	int AA = permutation[A] + Z;
	int AB = permutation[A + 1] + Z;
	int B = permutation[X + 1] + Y;
	int BA = permutation[B] + Z;
	int BB = permutation[B + 1] + Z;

	// 8つの角のグラディエントを補間
	float gradAA = Gradient(permutation[AA], x, y, z);
	float gradBA = Gradient(permutation[BA], x - 1.0f, y, z);
	float gradAB = Gradient(permutation[AB], x, y - 1.0f, z);
	float gradBB = Gradient(permutation[BB], x - 1.0f, y - 1.0f, z);
	float gradAA1 = Gradient(permutation[AA + 1], x, y, z - 1.0f);
	float gradBA1 = Gradient(permutation[BA + 1], x - 1.0f, y, z - 1.0f);
	float gradAB1 = Gradient(permutation[AB + 1], x, y - 1.0f, z - 1.0f);
	float gradBB1 = Gradient(permutation[BB + 1], x - 1.0f, y - 1.0f, z - 1.0f);

	// 補間を実行
	float x1 = Lerp(gradAA, gradBA, u);
	float x2 = Lerp(gradAB, gradBB, u);
	float y1 = Lerp(x1, x2, v);

	float x3 = Lerp(gradAA1, gradBA1, u);
	float x4 = Lerp(gradAB1, gradBB1, u);
	float y2 = Lerp(x3, x4, v);

	return Lerp(y1, y2, w);
}

float NoiseModule::Fade(float t) {
	// 6t^5 - 15t^4 + 10t^3
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float NoiseModule::Lerp(float a, float b, float t) {
	return a + t * (b - a);
}

float NoiseModule::Gradient(int hash, float x, float y, float z) {
	// 下位4ビットを使用してグラディエント方向を決定
	int h = hash & 15;
	float u = h < 8 ? x : y;
	float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
	return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}
}
