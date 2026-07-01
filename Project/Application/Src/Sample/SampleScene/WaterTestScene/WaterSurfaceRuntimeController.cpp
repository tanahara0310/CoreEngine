#include "pch.h"
#include "WaterSurfaceRuntimeController.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Water/Simulation/FFTOceanSurfaceSimulator.h"
#include "Graphics/Water/Simulation/GerstnerWaterSimulator.h"
#include "Graphics/Water/FFTOceanManager.h"
#include "Utility/Logger/Logger.h"

using namespace CoreEngine;

void WaterSurfaceRuntimeController::Initialize(const WaterSceneObjects& sceneObjects) {
	// setup 済みオブジェクト参照を受け取り、runtime 更新対象を確定する
	waterPlane_ = sceneObjects.waterPlane;
	groundObject_ = sceneObjects.groundObject;

	if (!gerstnerSimulator_) {
		gerstnerSimulator_ = std::make_unique<GerstnerWaterSimulator>();
	}

	if (!fftOceanSimulator_) {
		fftOceanSimulator_ = std::make_unique<FFTOceanSurfaceSimulator>();
	}

	if (waterPlane_) {
		if (auto* activeSimulator = GetActiveSimulator()) {
			waterPlane_->SetSimulationTime(activeSimulator->GetElapsedTime());
		}
	}

	UpdateWaterRefractionSurfaceData();
}

void WaterSurfaceRuntimeController::UpdateSimulation(float deltaTime) {
	if (!waterPlane_) {
		surfaceModelProvider_.SetSource(nullptr, WaterSurfaceSimulationType::Gerstner, "WaterSurfaceRuntimeController");
		return;
	}

	if (auto* activeSimulator = GetActiveSimulator()) {
		activeSimulator->AdvanceSimulation(deltaTime);
		waterPlane_->UpdateUVAnimation(deltaTime);
		waterPlane_->SetSimulationTime(activeSimulator->GetElapsedTime());
	}
}

void WaterSurfaceRuntimeController::SyncFrameResources(EngineSystem& engine) {
	const IWaterSurfaceModelProvider* surfaceModelProvider = waterPlane_ ? &surfaceModelProvider_ : nullptr;
	if (auto* renderDomainContext = engine.GetRenderDomainContext()) {
		if (auto* waterRefractionManager = renderDomainContext->GetWaterRefractionRayTracingManager()) {
			waterRefractionManager->SetSurfaceModelProvider(surfaceModelProvider);
		}
		if (auto* waterCausticsManager = renderDomainContext->GetWaterCausticsRayTracingManager()) {
			waterCausticsManager->SetSurfaceModelProvider(surfaceModelProvider);
		}
	}

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
	waterSurfaceSnapshot_ = {};
	waterRefractionSurfaceData_ = {};
	if (!waterPlane_) {
		return;
	}

	WaterSurfaceSimulationInput simulationInput{};
	simulationInput.waterHeight = waterPlane_->GetTransform().translate.y;
	simulationInput.gerstnerConstants = &waterPlane_->GetWaterConstants();

	if (auto* activeSimulator = GetActiveSimulator()) {
		activeSimulator->CaptureSurface(
			simulationInput,
			waterSurfaceSnapshot_,
			waterRefractionSurfaceData_);
		surfaceModelProvider_.SetSource(
			&waterRefractionSurfaceData_,
			activeSimulator->GetSimulationType(),
			"WaterSurfaceRuntimeController");
	} else {
		surfaceModelProvider_.SetSource(
			&waterRefractionSurfaceData_,
			WaterSurfaceSimulationType::Gerstner,
			"WaterSurfaceRuntimeController");
	}

	static uint32_t sSurfaceDataLogCounter = 0u;
	if ((sSurfaceDataLogCounter++ % 120u) == 0u) {
		Logger::GetInstance().Infof(
			LogCategory::Graphics,
			LogSubCategory::Pipeline,
			"WaterSurfaceRuntimeController: simulationType={} surfaceData time={:.3f} activeWaveCount={} height={:.3f}",
			static_cast<uint32_t>(waterPlane_->IsUsingFFTOcean()
				? WaterSurfaceSimulationType::FFTOcean
				: WaterSurfaceSimulationType::Gerstner),
			waterRefractionSurfaceData_.time,
			waterRefractionSurfaceData_.activeWaveCount,
			waterRefractionSurfaceData_.waterHeight);
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

WaterSurfaceSimulator* WaterSurfaceRuntimeController::GetActiveSimulator() const {
	if (!waterPlane_) {
		return nullptr;
	}

	return waterPlane_->IsUsingFFTOcean()
		? fftOceanSimulator_.get()
		: gerstnerSimulator_.get();
}
