#pragma once

#include "Graphics/Water/WaterSurfaceData.h"
#include "Sample/TestGameObject/Model/ModelObject.h"
#include "Sample/TestGameObject/Primitive/WaterPlaneObject.h"

class WaterTestScene;

namespace CoreEngine {
	class EngineSystem;
	struct RenderViewResult;
}

class WaterSurfaceRuntimeController {
public:
	/// @brief 水面関連オブジェクトを生成し初期状態を設定する
	/// @param scene 水面オブジェクト生成元のシーン
	/// @param engine エンジンシステム
	void Initialize(WaterTestScene& scene, CoreEngine::EngineSystem& engine);

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
	/// @brief 水面シーンで使用するオブジェクト群を生成する
	void CreateSceneObjects(WaterTestScene& scene, CoreEngine::EngineSystem& engine);
	/// @brief 水面マテリアルの初期値を設定する
	void ConfigureWaterMaterial();
	/// @brief 水中地形モデルの初期状態を設定する
	void ConfigureGroundObject();

	/// @brief 水面描画本体
	WaterPlaneObject* waterPlane_ = nullptr;
	/// @brief 水中地形モデル
	ModelObject* groundObject_ = nullptr;
	/// @brief DXR 屈折用に転送する水面データ
	CoreEngine::WaterSurfaceData waterRefractionSurfaceData_{};
};
