#pragma once

#include "ParticleModule.h"
#include "MathCore.h"

struct Particle;

/// @brief パーティクルの速度モジュール
/// 注意: 初期速度の大きさはMainModuleで設定します
/// このモジュールは速度の方向のみを決定します

namespace CoreEngine
{
class VelocityModule : public ParticleModule {
public:
	struct VelocityData {
		// 速度の方向（自動的に正規化されます）
		Vector3 startSpeed = { 0.0f, 1.0f, 0.0f };
		
		// 方向のランダム範囲（正規化後に適用）
		Vector3 randomSpeedRange = { 1.0f, 1.0f, 1.0f };
		
		// 完全ランダム方向を使用するか
		bool useRandomDirection = true;
	};

	VelocityModule();
	~VelocityModule() = default;

	/// @brief 速度データを設定
	/// @param data 速度データ
	void SetVelocityData(const VelocityData& data) { velocityData_ = data; }

	/// @brief 速度データを取得
	/// @return 速度データの参照
	const VelocityData& GetVelocityData() const { return velocityData_; }

	/// @brief パーティクルに初期速度を適用
	/// @param particle 対象のパーティクル
	void ApplyInitialVelocity(Particle& particle);

#ifdef _DEBUG
	/// @brief ImGuiデバッグ表示
	/// @return UIに変更があった場合true
	bool ShowImGui() override;
#endif

private:
	VelocityData velocityData_;

	/// @brief ランダムな方向ベクトルを生成
	/// @return ランダムな方向ベクトル
	Vector3 GenerateRandomDirection();
};
}
