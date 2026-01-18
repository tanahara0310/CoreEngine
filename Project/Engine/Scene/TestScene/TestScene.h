#pragma once

#include <cassert>
#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <numbers>
#include <random>
#include <vector>
#include <memory>

// エンジンコア
#include "Engine/Audio/SoundManager.h"
#include "Engine/Camera/CameraManager.h"
#include "Engine/Camera/Debug/DebugCamera.h"
#include "Engine/Camera/Release/Camera.h"
#include "MathCore.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/TextureManager.h"
#include "Engine/Graphics/Light/LightData.h"
#include "Engine/Graphics/Model/ModelManager.h"
#include "Engine/Graphics/Model/Model.h"

// シーン関連
#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"

// GameObjectのインクルード
#include "TestGameObject/SphereObject.h"
#include "TestGameObject/FenceObject.h"
#include "TestGameObject/TerrainObject.h"
#include "TestGameObject/AnimatedCubeObject.h"
#include "TestGameObject/SkeletonModelObject.h"
#include "TestGameObject/WalkModelObject.h"
#include "TestGameObject/SneakWalkModelObject.h"
#include "TestGameObject/SkyBoxObject.h"
#include "ObjectCommon/SpriteObject.h"
#include "ObjectCommon/TextObject.h"

// パーティクルシステム
#include "Engine/Particle/ParticleSystem.h"

using namespace Microsoft::WRL;

/// @brief テストシーンクラス

namespace CoreEngine
{
class TestScene : public BaseScene {
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

private: // メンバ変数

	Logger& logger = Logger::GetInstance();

	// ===== パーティクルシステム =====
	ParticleSystem* particleSystem_;  // パーティクルシステム
	ParticleSystem* modelParticleSystem_;  // モデルパーティクルシステム
	std::unique_ptr<Model> sphereModelForParticle_;  // パーティクル用sphereモデル

	// ===== テクスチャ =====
	TextureManager::LoadedTexture textureChecker_;
	TextureManager::LoadedTexture textureCircle_;

	// ===== サウンドリソース =====
	std::unique_ptr<SoundManager::SoundResource> mp3Resource_;  // 自動管理されるMP3リソース

	// サウンド制御用のUI変数
	float masterVolume_ = 1.0f;
	float mp3Volume_ = 1.0f;
	bool soundLoaded_ = false;
};
}
