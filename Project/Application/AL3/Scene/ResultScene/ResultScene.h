#pragma once

#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"

/// @brief リザルトシーンクラス
class ResultScene : public CoreEngine::BaseScene {
public:
	/// @brief 初期化
	void Initialize(CoreEngine::EngineSystem* engine) override;

	/// @brief 描画処理
	void Draw() override;

	/// @brief 解放
	void Finalize() override;

protected:
	/// @brief 更新
	void OnUpdate() override;
};
