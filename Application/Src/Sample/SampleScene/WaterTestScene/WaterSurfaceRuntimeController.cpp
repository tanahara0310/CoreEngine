#include "pch.h"
#include "WaterSurfaceRuntimeController.h"

#include "../../../../../Project/Application/Src/Sample/SampleScene/WaterTestScene/WaterTestScene.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/IBL/IBLSystem.h"
#include "Graphics/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Water/FFTOceanManager.h"
#include "Sample/TestGameObject/SkyBox/SkyBoxObject.h"
#include "Utility/Logger/Logger.h"

using namespace CoreEngine;

namespace {
// WaterPlaneObject の波情報を DXR 屈折用レイアウトへ変換する。
WaterWaveParam ConvertToWaterRefractionWaveParam(const WaveParams& wave) {
	WaterWaveParam result{};
	result.direction[0] = wave.direction.x;
	result.direction[1] = wave.direction.y;
	result.amplitude = wave.amplitude;
	result.wavelength = wave.wavelength;
	result.speed = wave.speed;
	result.steepness = wave.steepness;
	result.phaseOffset = wave.phaseOffset;
	result.padding = wave.padding;
	return result;
}
}

void WaterSurfaceRuntimeController::Initialize(WaterTestScene& scene, EngineSystem& engine) {
	// 水面シーンに必要なオブジェクトを生成する
	CreateSceneObjects(scene, engine);
	if (!waterPlane_) {
		return;
	}

	// 水面描画の初期パラメータを設定する
	ConfigureWaterMaterial();
	waterPlane_->SetScrollSpeed({ 0.03f, 0.01f });
	waterPlane_->SetUVTiling({ 4.0f, 4.0f });
	waterPlane_->SetActive(true);

	// 水中地形を有効化し、初期の水面データを構築する
	ConfigureGroundObject();
	UpdateWaterRefractionSurfaceData();
}

void WaterSurfaceRuntimeController::UpdateSimulation(float deltaTime) {
	if (waterPlane_) {
		waterPlane_->UpdateUVScroll(deltaTime);
	}
}

void WaterSurfaceRuntimeController::SyncFrameResources(EngineSystem& engine) {
	if (!waterPlane_) {
		return;
	}

	// 水越しの背景色として使用するシーンカラー SRV を更新する
	if (auto* render = engine.GetComponent<Render>()) {
		if (auto* renderTargetManager = render->GetRenderTargetManager()) {
			RenderTarget* sceneColorTarget = renderTargetManager->GetRenderTarget(RenderTargetNames::SceneColorSnapshot);
			if (!sceneColorTarget) {
				sceneColorTarget = renderTargetManager->GetRenderTarget(RenderTargetNames::SceneColor);
			}

			if (sceneColorTarget) {
				waterPlane_->SetSceneColorSRV(sceneColorTarget->GetSRVHandle());
			}
		}
	}

	// Depth Fade 用のシーン深度 SRV を設定する
	if (auto* dxCommon = engine.GetComponent<DirectXCommon>()) {
		waterPlane_->SetSceneDepthSRV(dxCommon->GetDepthStencilSRV());
	}

	// DXR 屈折結果のカラー SRV を設定する
	if (auto* renderDomainContext = engine.GetRenderDomainContext()) {
		if (auto* waterRefractionManager = renderDomainContext->GetWaterRefractionRayTracingManager()) {
			waterPlane_->SetRefractionColorSRV(
				waterRefractionManager->GetRefractionSRVHandle(
					WaterRefractionRayTracingManager::ViewID::GameView));
		}

		if (auto* fftOceanManager = renderDomainContext->GetFFTOceanManager()) {
			waterPlane_->SetFFTOceanTextureSRVs(
				fftOceanManager->GetDisplacementSRVHandle(),
				fftOceanManager->GetNormalSRVHandle(),
				fftOceanManager->GetJacobianSRVHandle());
		}
	}
}

void WaterSurfaceRuntimeController::UpdateWaterRefractionSurfaceData() {
	// 前フレームの値を破棄し、現在の水面状態から再構築する
	waterRefractionSurfaceData_ = {};
	if (!waterPlane_) {
		return;
	}

	// 水面高さ・有効波数・波時間を DXR 側参照用にコピーする
	const WaterConstants& waterConstants = waterPlane_->GetWaterConstants();
	waterRefractionSurfaceData_.waterHeight = waterPlane_->GetTransform().translate.y;
	waterRefractionSurfaceData_.activeWaveCount = (std::min)(waterConstants.activeWaveCount, kMaxWaterSurfaceWaveCount);
	waterRefractionSurfaceData_.time = waterConstants.time;

	static uint32_t sSurfaceDataLogCounter = 0u;
	if ((sSurfaceDataLogCounter++ % 120u) == 0u) {
		Logger::GetInstance().Infof(
			LogCategory::Graphics,
			LogSubCategory::Pipeline,
			"WaterSurfaceRuntimeController: surfaceData time={:.3f} waterCB.time={:.3f} activeWaveCount={} height={:.3f}",
			waterRefractionSurfaceData_.time,
			waterConstants.time,
			waterRefractionSurfaceData_.activeWaveCount,
			waterRefractionSurfaceData_.waterHeight);
	}

	// アクティブな波のみを DXR 用の配列へ詰め直す
	for (uint32_t waveIndex = 0; waveIndex < waterRefractionSurfaceData_.activeWaveCount; ++waveIndex) {
		waterRefractionSurfaceData_.waves[waveIndex] =
			ConvertToWaterRefractionWaveParam(waterConstants.waves[waveIndex]);
	}
}

void WaterSurfaceRuntimeController::ApplyWaterRenderViewResult(const RenderViewResult& result) {
	if (waterPlane_) {
		waterPlane_->ApplyWaterReflectionResult(result);
	}
}

float WaterSurfaceRuntimeController::GetWaterHeight() const {
	return waterPlane_ ? waterPlane_->GetTransform().translate.y : 0.0f;
}

const WaterSurfaceData* WaterSurfaceRuntimeController::GetWaterRefractionSurfaceData() const {
	return waterPlane_ ? &waterRefractionSurfaceData_ : nullptr;
}

void WaterSurfaceRuntimeController::CreateSceneObjects(WaterTestScene& scene, EngineSystem& engine) {
	auto* iblSystem = engine.GetComponent<IBLSystem>();
	if (!iblSystem) {
		return;
	}

	// IBL 用の環境マップを読み込み、ライティング環境を初期化する
	auto& textureManager = TextureManager::GetInstance();
	auto environmentMapTexture = textureManager.Load("kloppenheim_06_puresky_4k.hdr");

	IBLSystem::SetupParams iblParams;
	iblParams.environmentMap = environmentMapTexture.texture.Get();
	iblParams.environmentMapSRV = environmentMapTexture.gpuHandle;
	iblParams.environmentKey = "kloppenheim_06_puresky_4k.hdr";
	iblParams.irradianceSize = 128;
	iblParams.prefilteredSize = 256;
	iblParams.brdfLUTSize = 512;
	iblSystem->Setup(iblParams);

	// SkyBox を生成して環境マップを可視化する
	auto skyBox = scene.CreateWaterSceneObject<SkyBoxObject>();
	skyBox->SetTexture(environmentMapTexture);
	skyBox->SetActive(true);

	// 水面グリッドを生成し、水面描画用の基本状態を設定する
	waterPlane_ = scene.CreateWaterSceneObject<WaterPlaneObject>(100.0f, 128, true);
	waterPlane_->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
	waterPlane_->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
	waterPlane_->SetBlendMode(BlendMode::kBlendModeNormal);

	// 水中地形モデルを生成する
	groundObject_ = scene.CreateWaterSceneObject<ModelObject>("pool.gltf");
}

void WaterSurfaceRuntimeController::ConfigureWaterMaterial() {
	if (!waterPlane_) {
		return;
	}

	if (auto* material = waterPlane_->GetModel()->GetMaterial()) {
		// 水面らしい鏡面的な PBR 初期値を設定する
		material->SetColor({ 0.04f, 0.18f, 0.28f, 0.85f });
		material->SetMetallic(0.0f);
		material->SetRoughness(0.04f);
		material->SetLightingEnabled(true);
		material->SetIBLEnabled(true);
		material->SetNormalMapEnabled(false);
		material->SetMetallicMapEnabled(false);
		material->SetRoughnessMapEnabled(false);
		material->SetAOMapEnabled(false);
	}
}

void WaterSurfaceRuntimeController::ConfigureGroundObject() {
	if (!groundObject_) {
		return;
	}

	// 水中地形モデルを水面直下へ配置し、PBR 描画を有効化する
	groundObject_->GetTransform().translate = { 0.0f, -0.1f, 0.0f };
	groundObject_->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
	groundObject_->SetPBRTextureMapsEnabled(true, true, true, true);
	groundObject_->SetIBLEnabled(true);
	groundObject_->SetActive(true);
}
