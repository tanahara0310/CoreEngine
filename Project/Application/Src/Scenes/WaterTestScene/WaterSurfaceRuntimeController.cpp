#include "pch.h"
#include "WaterSurfaceRuntimeController.h"

#include "EngineSystem/EngineSystem.h"
#include "Camera/CameraStructs.h"
#include "Camera/ICamera.h"
#include "Scene/SceneManager.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterReflectionRayTracingManager.h"
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
		if (auto* waterReflectionManager = renderDomainContext->GetWaterReflectionRayTracingManager()) {
			waterReflectionManager->SetSurfaceModelProvider(surfaceModelProvider);
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

	// Depth Fade の深度線形化に使うカメラのクリップ距離を毎フレーム同期する。
	// エディタでは far=100000 のデバッグカメラ、実行時は既定 far=1000 のリリースカメラと
	// 値が変わるため、シェーダー側に定数で持たせてはいけない（水柱厚さが狂う）。
	if (auto* sceneManager = engine.GetSceneManager()) {
		if (ICamera* camera = sceneManager->GetGameViewCamera3D()) {
			const CameraParameters cameraParams = camera->GetParameters();
			waterPlane_->SetCameraClipPlanes(cameraParams.nearClip, cameraParams.farClip);
		}
	}

	// DXR 屈折結果のカラー SRV を設定する
	if (auto* renderDomainContext = engine.GetRenderDomainContext()) {
		if (auto* waterRefractionManager = renderDomainContext->GetWaterRefractionRayTracingManager()) {
			waterPlane_->SetRefractionColorSRV(
				waterRefractionManager->GetRefractionSRVHandle(
					WaterRefractionRayTracingManager::ViewID::GameView));
		}

		// DXR 反射結果のカラー SRV を gReflectionTexture として設定する（鏡像カメラの置き換え）。
		// これで反射源がシーン複雑度から切り離される（島の数に依存しない固定コスト）。
		if (auto* waterReflectionManager = renderDomainContext->GetWaterReflectionRayTracingManager()) {
			waterPlane_->SetReflectionTexture(
				waterReflectionManager->GetReflectionSRVHandle(
					WaterReflectionRayTracingManager::ViewID::GameView));
		}

		if (auto* fftOceanManager = renderDomainContext->GetFFTOceanManager()) {
			waterPlane_->SetFFTOceanTextureSRVs(
				fftOceanManager->GetDisplacementSRVHandle(),
				fftOceanManager->GetNormalSRVHandle(),
				fftOceanManager->GetJacobianSRVHandle());
		}

		// 水面描画と同じ波長依存吸収係数 σa を RT コースティクスへ毎フレーム同期する
		// （Jerlov プリセット / 濁度 UI の変更に Beer–Lambert 減衰を追従させる）
		if (auto* waterCausticsManager = renderDomainContext->GetWaterCausticsRayTracingManager()) {
			const WaterFrameConstants& frameConstants = waterPlane_->GetFrameConstants();
			WaterCausticsRayTracingSettings causticsSettings = waterCausticsManager->GetSettings();
			causticsSettings.absorptionCoeff[0] = frameConstants.absorptionCoeff[0];
			causticsSettings.absorptionCoeff[1] = frameConstants.absorptionCoeff[1];
			causticsSettings.absorptionCoeff[2] = frameConstants.absorptionCoeff[2];
			waterCausticsManager->SetSettings(causticsSettings);
		}

		// 大気散乱（Aerial Perspective）のリソースを水面へ接続する。
		// IsAtmosphereActive() はこの時点（OnUpdate）ではまだ立っていないため、
		// シーン側から受け取った SkyBox の大気モード + LUT/CB の準備完了で判定する
		if (auto* atmosphereManager = renderDomainContext->GetAtmosphereManager()) {
			const bool apEnabled = atmosphereSkyEnabled_
				&& atmosphereManager->IsConstantBufferReady()
				&& atmosphereManager->AreLUTsReady();
			waterPlane_->SetAtmosphereAPResources(
				atmosphereManager->GetConstantBufferGPUAddress(),
				atmosphereManager->GetCameraVolumeLUTSRVHandle(),
				atmosphereManager->GetSkyViewLUTSRVHandle(),
				apEnabled);

			// 水中インスキャッタの天空光として Sky Irradiance SH を接続する
			// （DeferredLighting の空アンビエントと同じソース・同じスケール）
			const bool skyAmbientEnabled = apEnabled
				&& atmosphereManager->IsSkyAmbientEnabled()
				&& atmosphereManager->IsSkyAmbientReady();
			waterPlane_->SetSkyAmbientResources(
				atmosphereManager->GetSkyIrradianceSHSRVHandle(),
				atmosphereManager->GetSkyAmbientScale(),
				skyAmbientEnabled);

			// 空スペキュラキューブマップ（空＋雲）を平面反射への雲合成用に接続する
			const bool skyEnvEnabled = apEnabled
				&& atmosphereManager->IsSkySpecularEnabled()
				&& atmosphereManager->IsSkyEnvironmentReady();
			waterPlane_->SetSkyEnvironmentReflection(
				atmosphereManager->GetSkySpecularSRVHandle(),
				skyEnvEnabled);
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

	// 水面オブジェクトが非表示のフレームは「水なし」として扱う。
	// ここで surface data（regionValid=0 のゼロ値）を publish しないと、
	// RT コースティクスのディスパッチと DeferredLighting の水中ライティング
	// （直接光のコースティクス置換・アンビエントの Beer–Lambert 減衰）が
	// 動き続け、非表示のはずの水の光学効果（薄い青色）が床に乗り続ける。
	if (!waterPlane_->IsActive()) {
		if (surfaceModelProvider_) {
			surfaceModelProvider_->SetSource(
				nullptr,
				WaterSurfaceSimulationType::Gerstner,
				"WaterSurfaceRuntimeController");
		}
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

		// FFT Ocean の DXR 側サンプリングがラスタ描画（FFTWater.VS）と同一の波面を
		// 評価できるよう、ワールドXZ → FFT テクスチャ UV の写像を水面メッシュの
		// トランスフォームから導出して渡す。
		// FFTWater.VS: sampleUV = (worldXZ - translate)/ローカルサイズ + scale/2
		// （メッシュの texcoord は {u, 1-v} で生成されるが、VS 側で V を戻して
		//   「テクスチャ +v = ワールド +Z」に統一済み。回転は非対応＝ゼロ前提）
		const float localSize = waterPlane_->GetSize();
		if (localSize > 1.0e-4f) {
			const auto& transform = waterPlane_->GetTransform();
			waterRefractionSurfaceData_.fftUVScale[0] = 1.0f / localSize;
			waterRefractionSurfaceData_.fftUVScale[1] = 1.0f / localSize;
			waterRefractionSurfaceData_.fftUVOffset[0] =
				0.5f * transform.scale.x - transform.translate.x / localSize;
			waterRefractionSurfaceData_.fftUVOffset[1] =
				0.5f * transform.scale.z - transform.translate.z / localSize;
			waterRefractionSurfaceData_.fftUVMappingValid = 1;

			// 水面メッシュのワールドXZ範囲。コースティクスが水域の外
			// （無限市松床など）へ漏れないようにするための受光マスク。
			// メッシュはローカル ±localSize/2 に広がる正方形（回転非対応の前提は
			// fftUV 写像と同じ）なので、ワールド半径は 0.5 * localSize * scale。
			waterRefractionSurfaceData_.regionCenterXZ[0] = transform.translate.x;
			waterRefractionSurfaceData_.regionCenterXZ[1] = transform.translate.z;
			waterRefractionSurfaceData_.regionHalfExtentXZ[0] = 0.5f * localSize * transform.scale.x;
			waterRefractionSurfaceData_.regionHalfExtentXZ[1] = 0.5f * localSize * transform.scale.z;
			waterRefractionSurfaceData_.regionValid = 1;
		}
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
