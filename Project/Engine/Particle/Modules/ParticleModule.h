#pragma once

#include "MathCore.h"
#include "Engine/Utility/Random/RandomGenerator.h"

#ifdef _DEBUG
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
#endif

namespace CoreEngine
{
// 前方宣言
struct Particle;

/// @brief パーティクルモジュールの基底クラス
class ParticleModule {
public:
	ParticleModule() = default;
	virtual ~ParticleModule() = default;

	/// @brief モジュールの有効/無効を設定
	/// @param enabled 有効にするかどうか
	void SetEnabled(bool enabled) { enabled_ = enabled; }

	/// @brief モジュールが有効かどうか
	/// @return 有効な場合true
	bool IsEnabled() const { return enabled_; }

#ifdef _DEBUG
	/// @brief ImGuiデバッグ表示（純粋仮想関数）
	/// @return UIに変更があった場合true
	virtual bool ShowImGui() = 0;
#endif

protected:
	bool enabled_ = true;
	RandomGenerator& random_ = RandomGenerator::GetInstance(); // 統一乱数生成器への参照

	/// @brief ランダム性を適用したヘルパー関数
	/// @param baseValue 基本値
	/// @param randomness ランダム性の範囲
	/// @return ランダム性を適用した値
	float ApplyRandomness(float baseValue, float randomness) {
		if (randomness <= 0.0f) return baseValue;
		return baseValue + random_.GetFloat(-randomness, randomness);
	}
};
}
