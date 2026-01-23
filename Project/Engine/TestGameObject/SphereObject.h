#pragma once

#include "Engine/ObjectCommon/GameObject.h"

/// @brief Sphereモデルオブジェクト

namespace CoreEngine
{
class SphereObject : public GameObject {
public:
	/// @brief 初期化処理
	/// @param engine エンジンシステムへのポインタ
	void Initialize();

	/// @brief 更新処理
	void Update() override;

	/// @brief 描画処理
	/// @param camera カメラ
	void Draw(const ICamera* camera) override;

#ifdef _DEBUG
	/// @brief オブジェクト名を取得
	/// @return オブジェクト名
	const char* GetObjectName() const override { return "Sphere"; }
#endif

	/// @brief トランスフォームを取得
	WorldTransform& GetTransform() { return transform_; }

	/// @brief モデルを取得
	Model* GetModel() { return model_.get(); }
};
}
