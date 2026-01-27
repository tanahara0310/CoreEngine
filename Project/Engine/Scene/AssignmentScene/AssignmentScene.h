#pragma once

#include <memory>
#include <vector>

// エンジンコア
#include "MathCore.h"
#include "Engine/Graphics/Light/LightData.h"

// シーン関連
#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"

// GameObjectのインクルード
#include "TestGameObject/SphereObject.h"
#include "TestGameObject/Plane.h"
#include "TestGameObject/WalkModelObject.h"

/// @brief 課題専用シーン
/// Sphere、Terrainモデル、複数のPointLight、複数のSpotLightを表示

namespace CoreEngine
{
	class AssignmentScene : public BaseScene {
	public:
		/// @brief 初期化
		void Initialize(EngineSystem* engine) override;

		/// @brief 描画処理
		void Draw() override;

		/// @brief 解放
		void Finalize() override;

	protected:
		/// @brief 更新処理（BaseSceneのOnUpdate()をオーバーライド）
		void OnUpdate() override;

	private:
	// オブジェクトへのポインタ（デバッグUI用）
	SphereObject* sphere_ = nullptr;
	Plane* plane_ = nullptr;
	WalkModelObject* walkModel1_ = nullptr;
	WalkModelObject* walkModel2_ = nullptr;
	};
}
