#pragma once

#include "Engine/ObjectCommon/GameObject.h"

/// @brief 床（Floor）オブジェクト
class FloorObject : public CoreEngine::GameObject {
public:
	/// @brief 初期化処理
	void Initialize();

	/// @brief 更新処理
	void Update() override;

	/// @brief 描画処理
	/// @param camera カメラ
	void Draw(const CoreEngine::ICamera* camera) override;

	CoreEngine::RenderPassType GetRenderPassType() const override { return CoreEngine::RenderPassType::Model; }

#ifdef _DEBUG
	/// @brief オブジェクト名を取得
	/// @return オブジェクト名
	const char* GetObjectName() const override { return "Floor"; }
#endif

	/// @brief トランスフォームを取得
	CoreEngine::WorldTransform& GetTransform() { return transform_; }

	/// @brief モデルを取得
	CoreEngine::Model* GetModel() { return model_.get(); }
};
