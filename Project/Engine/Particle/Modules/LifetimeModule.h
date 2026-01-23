#pragma once

#include "ParticleModule.h"

struct Particle;

/// @brief パーティクルのライフタイムモジュール

namespace CoreEngine
{
class LifetimeModule : public ParticleModule {
public:
	struct LifetimeData {
		float startLifetime = 5.0f;         // 開始ライフタイム
		float lifetimeRandomness = 0.0f;    // ライフタイムのランダム性 (0.0f - 1.0f)
	};

	LifetimeModule() = default;
	~LifetimeModule() = default;

	/// @brief ライフタイムデータを設定
	/// @param data ライフタイムデータ
	void SetLifetimeData(const LifetimeData& data) { lifetimeData_ = data; }

	/// @brief ライフタイムデータを取得
	/// @return ライフタイムデータの参照
	const LifetimeData& GetLifetimeData() const { return lifetimeData_; }

	/// @brief パーティクルに初期ライフタイムを適用
	/// @param particle 対象のパーティクル
	void ApplyInitialLifetime(Particle& particle);

	/// @brief パーティクルのライフタイムを更新
	/// @param particle 対象のパーティクル
	/// @param deltaTime フレーム時間
	/// @return パーティクルが生きている場合true、死んでいる場合false
	bool UpdateLifetime(Particle& particle, float deltaTime);

#ifdef _DEBUG
	/// @brief ImGuiデバッグ表示
	/// @return UIに変更があった場合true
	bool ShowImGui() override;
#endif

private:
	LifetimeData lifetimeData_;
};
}
