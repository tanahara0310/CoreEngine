#include "pch.h"
#include "WaterSurfaceRuntimeController.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
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

	// 描画経路切り替えに備えて、Gerstner / FFT の両 simulator を初期化する
	if (!gerstnerSimulator_) {
		gerstnerSimulator_ = std::make_unique<GerstnerWaterSimulator>();
	}

	if (!fftOceanSimulator_) {
		fftOceanSimulator_ = std::make_unique<FFTOceanSurfaceSimulator>();
	}

	if (!surfaceModelProvider_) {
		surfaceModelProvider_ = std::make_shared<StaticWaterSurfaceModelProvider>(
			nullptr,
			WaterSurfaceSimulationType::Gerstner,
			"WaterSurfaceRuntimeController");
	}

	if (waterPlane_) {
		// 初期フレームでも水面シェーダーの時間が不定にならないよう、
		// アクティブ simulator 側の時間を即時反映する
		if (auto* activeSimulator = GetActiveSimulator()) {
			waterPlane_->SetSimulationTime(activeSimulator->GetElapsedTime());
		}
	}

	UpdateWaterRefractionSurfaceData();
}

void WaterSurfaceRuntimeController::UpdateSimulation(float deltaTime) {
	if (!waterPlane_) {
		// 水面が存在しないフレームでは、RT 側がフォールバックへ切り替わるよう
		// provider の参照元を明示的に null にする
		if (surfaceModelProvider_) {
			surfaceModelProvider_->SetSource(nullptr, WaterSurfaceSimulationType::Gerstner, "WaterSurfaceRuntimeController");
		}
		return;
	}

	if (auto* activeSimulator = GetActiveSimulator()) {
		// シミュレーション時間の進行と表示用UVアニメーションを同期させる
		activeSimulator->AdvanceSimulation(deltaTime);
		waterPlane_->UpdateUVAnimation(deltaTime);
		waterPlane_->SetSimulationTime(activeSimulator->GetElapsedTime());
	}
}

void WaterSurfaceRuntimeController::SyncFrameResources(EngineSystem& engine) {
	// RT マネージャーへ provider を毎フレーム接続し、
	// Gerstner / FFT 切り替え結果を屈折・コースティクスへ伝播する
	const std::shared_ptr<const IWaterSurfaceModelProvider> surfaceModelProvider =
		waterPlane_ ? std::static_pointer_cast<const IWaterSurfaceModelProvider>(surfaceModelProvider_)
		: std::shared_ptr<const IWaterSurfaceModelProvider>{};
	if (auto* renderDomainContext = engine.GetRenderDomainContext()) {
		if (auto* waterRefractionManager = renderDomainContext->GetWaterRefractionRayTracingManager()) {
			waterRefractionManager->SetSurfaceModelProvider(surfaceModelProvider);
		}
		if (auto* waterCausticsManager = renderDomainContext->GetWaterCausticsRayTracingManager()) {
			waterCausticsManager->SetSurfaceModelProvider(surfaceModelProvider);
		}
	}

	if (!waterPlane_) {
		// provider 接続だけは維持し、描画リソース同期は水面オブジェクト不在時に省略する
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
	// 水面オブジェクトの現在状態を simulator へ入力し、
	// 共通 snapshot と DXR surface data を同時に更新する
	simulationInput.waterHeight = waterPlane_->GetTransform().translate.y;
	simulationInput.gerstnerConstants = &waterPlane_->GetWaterConstants();

	if (auto* activeSimulator = GetActiveSimulator()) {
		// 実際に有効な simulator 種別で surface data を構築する
		activeSimulator->CaptureSurface(
			simulationInput,
			waterSurfaceSnapshot_,
			waterRefractionSurfaceData_);
		// RT 側へ渡す provider の参照先を最新 surface data に更新する
		if (surfaceModelProvider_) {
			surfaceModelProvider_->SetSource(
				&waterRefractionSurfaceData_,
				activeSimulator->GetSimulationType(),
				"WaterSurfaceRuntimeController");
		}
	} else {
		// simulator 不在時も provider の参照先を維持し、
		// RT 側がゼロ初期化データへ安全にフォールバックできるようにする
		if (surfaceModelProvider_) {
			surfaceModelProvider_->SetSource(
				&waterRefractionSurfaceData_,
				WaterSurfaceSimulationType::Gerstner,
				"WaterSurfaceRuntimeController");
		}
	}

	static uint32_t sSurfaceDataLogCounter = 0;
	if ((sSurfaceDataLogCounter++ % 120) == 0) {
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
