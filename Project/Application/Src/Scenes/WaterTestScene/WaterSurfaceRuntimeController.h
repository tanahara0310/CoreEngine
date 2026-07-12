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
	/// @details Gerstner/FFT の simulator を準備し、初期フレームの水面データを構築する。
	void Initialize(const WaterSceneObjects& sceneObjects);

	/// @brief 水面の時間進行と UV アニメーションを更新する
	/// @param deltaTime 前フレームからの経過時間（秒）
	void UpdateSimulation(float deltaTime);

	/// @brief 水面描画に必要な SRV とフレームリソースを同期する
	/// @param engine エンジンシステム
	/// @details RTマネージャーへの surface provider 接続、および水面描画に必要な
	/// SceneColor / SceneDepth / RT屈折 / FFTテクスチャを同期する。
	void SyncFrameResources(CoreEngine::EngineSystem& engine);

	/// @brief DXR 屈折用の水面サーフェスデータを更新する
	/// @details 現在有効な simulator から snapshot と surface data を再構築し、
	/// RT屈折・コースティクスで共有する provider の参照元を更新する。
	void UpdateWaterRefractionSurfaceData();

	/// @brief ReflectionView の描画結果を水面へ反映する
	/// @param result 反射描画結果
	void ApplyWaterRenderViewResult(const CoreEngine::RenderViewResult& result);

	/// @brief 管理中の水面オブジェクトを返す
	/// @return 管理対象の水面オブジェクト。未初期化時は nullptr。
	WaterPlaneObject* GetWaterPlane() const { return waterPlane_; }

	/// @brief 現在の水面高さを返す
	/// @return 水面オブジェクトのワールドY座標。未初期化時は 0.0f。
	float GetWaterHeight() const;

	/// @brief 現在の水面サーフェスデータを返す
	/// @return 現在フレームの surface data。未初期化時は nullptr。
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
	/// @brief RT 水面パスへ渡す surface model provider（寿命管理付き）
	std::shared_ptr<CoreEngine::StaticWaterSurfaceModelProvider> surfaceModelProvider_{};
	/// @brief Gerstner Wave 用 simulator
	std::unique_ptr<CoreEngine::WaterSurfaceSimulator> gerstnerSimulator_{};
	/// @brief FFT Ocean 用 simulator
	std::unique_ptr<CoreEngine::WaterSurfaceSimulator> fftOceanSimulator_{};
};
