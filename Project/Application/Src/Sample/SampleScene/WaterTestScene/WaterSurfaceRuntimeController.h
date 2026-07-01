#pragma once

#include "Graphics/Water/WaterSurfaceData.h"
#include "Graphics/Water/Simulation/WaterSurfaceModelProvider.h"
#include "Graphics/Water/Simulation/WaterSurfaceSimulator.h"
#include "WaterSceneSetup.h"
#include "Sample/TestGameObject/Model/ModelObject.h"
#include "Sample/TestGameObject/Primitive/WaterPlaneObject.h"

#include <memory>

namespace CoreEngine {
	class EngineSystem;
	struct RenderViewResult;
}

class WaterSurfaceRuntimeController {
public:
	/// @brief setup 済みの水面関連オブジェクトを受け取り、runtime 制御を初期化する
	/// @param sceneObjects WaterSceneSetup が構築したオブジェクト群
	void Initialize(const WaterSceneObjects& sceneObjects);

	/// @brief 水面の時間進行と UV アニメーションを更新する
	/// @param deltaTime 前フレームからの経過時間（秒）
	void UpdateSimulation(float deltaTime);

	/// @brief 水面描画に必要な SRV とフレームリソースを同期する
	/// @param engine エンジンシステム
	void SyncFrameResources(CoreEngine::EngineSystem& engine);

	/// @brief DXR 屈折用の水面サーフェスデータを更新する
	void UpdateWaterRefractionSurfaceData();

	/// @brief ReflectionView の描画結果を水面へ反映する
	/// @param result 反射描画結果
	void ApplyWaterRenderViewResult(const CoreEngine::RenderViewResult& result);

	/// @brief 管理中の水面オブジェクトを返す
	WaterPlaneObject* GetWaterPlane() const { return waterPlane_; }

	/// @brief 現在の水面高さを返す
	float GetWaterHeight() const;

	/// @brief 現在の水面サーフェスデータを返す
	const CoreEngine::WaterSurfaceData* GetWaterRefractionSurfaceData() const;

private:
	/// @brief 現在の描画経路に対応する simulator を返す
	CoreEngine::WaterSurfaceSimulator* GetActiveSimulator() const;

	/// @brief 水面描画本体
	WaterPlaneObject* waterPlane_ = nullptr;
	/// @brief 水中地形モデル
	ModelObject* groundObject_ = nullptr;
	/// @brief 共通 surface snapshot
	CoreEngine::WaterSurfaceSnapshot waterSurfaceSnapshot_{};
	/// @brief DXR 屈折用に転送する水面データ
	CoreEngine::WaterSurfaceData waterRefractionSurfaceData_{};
	/// @brief RT 水面パスへ渡す surface model provider
	CoreEngine::StaticWaterSurfaceModelProvider surfaceModelProvider_{
		nullptr,
		CoreEngine::WaterSurfaceSimulationType::Gerstner,
		"WaterSurfaceRuntimeController" };
	/// @brief Gerstner Wave 用 simulator
	std::unique_ptr<CoreEngine::WaterSurfaceSimulator> gerstnerSimulator_{};
	/// @brief FFT Ocean 用 simulator
	std::unique_ptr<CoreEngine::WaterSurfaceSimulator> fftOceanSimulator_{};
};
