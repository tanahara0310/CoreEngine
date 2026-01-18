#pragma once

#include <list>
#include <cstdint>

namespace CoreEngine
{
// 前方宣言
struct Particle;
class ForceModule;
class ColorModule;
class SizeModule;
class RotationModule;
class NoiseModule;

/// @brief パーティクル更新処理クラス
/// パーティクルの物理演算・色・サイズ・回転などの更新を担当
class ParticleUpdater {
public:
	ParticleUpdater() = default;
	~ParticleUpdater() = default;

	/// @brief 初期化（モジュールへの参照を設定）
	/// @param forceModule 力場モジュール
 /// @param colorModule 色モジュール
	/// @param sizeModule サイズモジュール
	/// @param rotationModule 回転モジュール
	/// @param noiseModule ノイズモジュール
	void Initialize(
		ForceModule* forceModule,
		ColorModule* colorModule,
		SizeModule* sizeModule,
		RotationModule* rotationModule,
		NoiseModule* noiseModule
	);

	/// @brief パーティクルリストを更新
	/// @param particles パーティクルリスト（参照渡し、寿命切れは削除）
	/// @param deltaTime フレーム時間
	/// @param gravityModifier 重力倍率（MainModuleから取得）
	/// @return 削除されたパーティクル数
	uint32_t UpdateParticles(
		std::list<Particle>& particles,
		float deltaTime,
		float gravityModifier
	);

private:
	/// @brief 単一パーティクルを更新
	/// @param particle パーティクル
	/// @param deltaTime フレーム時間
	/// @param gravityModifier 重力倍率
	void UpdateSingleParticle(Particle& particle, float deltaTime, float gravityModifier);

	// モジュールへの参照（ポインタ）
	ForceModule* forceModule_ = nullptr;
	ColorModule* colorModule_ = nullptr;
	SizeModule* sizeModule_ = nullptr;
	RotationModule* rotationModule_ = nullptr;
	NoiseModule* noiseModule_ = nullptr;
};
}
