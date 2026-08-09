#include "pch.h"
#include "WaterRenderFeature.h"

#include <algorithm>

#include "Camera/CameraStructs.h"
#include "Camera/Camera.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObjectManager.h"
#include "GameObjects/SkyBox/SkyBoxObject.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/RenderingTechnique/Lighting/WaterCausticsTechnique.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Water/FFTOceanManager.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterReflectionRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Water/WaterCVars.h"
#include "Graphics/Water/Simulation/FFTOceanSurfaceSimulator.h"
#include "Graphics/Water/Simulation/GerstnerWaterSimulator.h"
#include "Graphics/Water/Surface/WaterPlaneObject.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace {
        constexpr const char* kProviderName = "WaterRenderFeature";

        /// @brief 診断ログの出力間隔（フレーム）
        constexpr uint32_t kDiagnosticsLogInterval = 120u;
    }

    WaterRenderFeature::WaterRenderFeature(const Config& config)
        : config_(config) {
    }

    WaterRenderFeature::~WaterRenderFeature() = default;

    void WaterRenderFeature::Initialize(SceneContext& ctx)
    {
        // 描画経路切り替えに備えて、Gerstner / FFT の両 simulator を用意する
        if (!gerstnerSimulator_) {
            gerstnerSimulator_ = std::make_unique<GerstnerWaterSimulator>();
        }
        if (!fftOceanSimulator_) {
            fftOceanSimulator_ = std::make_unique<FFTOceanSurfaceSimulator>();
        }
        if (!surfaceModelProvider_) {
            surfaceModelProvider_ = std::make_shared<StaticWaterSurfaceModelProvider>(kProviderName);
        }

        AcquireWaterPlane(ctx);

        if (waterPlane_) {
            // 初期フレームでも水面シェーダーの時間が不定にならないよう即時反映する
            if (auto* activeSimulator = GetActiveSimulator()) {
                waterPlane_->SetSimulationTime(activeSimulator->GetElapsedTime());
            }
        }

        // publish 先を確保しておく（Finalize での取り消しにも使う）
        if (ctx.engine) {
            publishTarget_ = ctx.engine->GetRenderDomainContext();
        }

        RefreshWaterSurfaceState();
    }

    void WaterRenderFeature::PostSceneInitialize(SceneContext& ctx)
    {
        // 空気遠近感の適用可否は「シーンに空（大気散乱の SkyBox）があるか」で決まる。
        // AtmosphereManager::IsAtmosphereActive() はフレーム後半まで立たないため、
        // 空の有無そのものを見る（EnvironmentFeature が先に SkyBox を確定させている）。
        if (!ctx.gameObjectManager) {
            return;
        }
        // 具象型のダウンキャストではなく SceneTag で探す
        if (auto* tag = ctx.gameObjectManager->FindFirstComponent<SceneTagComponent<SkyBoxObject>>()) {
            skyBox_ = tag->Get();
        }
    }

    void WaterRenderFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        switch (phase) {
        case SceneUpdatePhase::PreObjectUpdate:
        {
            auto* frameRate = ctx.engine ? ctx.engine->GetService<FrameRateController>() : nullptr;
            const float deltaTime = frameRate ? frameRate->GetDeltaTime() : (1.0f / 60.0f);

            // FFT の時刻は水面の表示状態と独立に進める。
            // 表示・非表示や Gerstner への切り替えで巻き戻ると、再表示時に波形が飛ぶ。
            if (fftOceanSimulator_) {
                fftOceanSimulator_->AdvanceSimulation(deltaTime);
            }

            if (!waterPlane_) {
                break;
            }

            // Gerstner 側の時間進行と表示用 UV アニメーションを同期させる
            if (!waterPlane_->IsUsingFFTOcean() && gerstnerSimulator_) {
                gerstnerSimulator_->AdvanceSimulation(deltaTime);
            }
            if (auto* activeSimulator = GetActiveSimulator()) {
                waterPlane_->UpdateUVAnimation(deltaTime);
                waterPlane_->SetSimulationTime(activeSimulator->GetElapsedTime());
            }
            break;
        }
        case SceneUpdatePhase::PostLogic:
        {
            // 大気散乱（EnvironmentFeature）の更新後に結線する。
            // LUT / SH / キューブマップの準備完了はこの時点で確定している。
            RefreshWaterSurfaceState();

            auto* domain = ctx.engine ? ctx.engine->GetRenderDomainContext() : nullptr;
            if (!domain) {
                break;
            }
            publishTarget_ = domain;

            // Gerstner / FFT の切り替え結果を屈折・反射・コースティクスへ伝播する
            ConnectSurfaceModelProvider(*domain);

            // FFT の時刻とサーフェス状態を描画側へ publish する
            domain->PublishFFTOceanSimulationTime(
                fftOceanSimulator_ ? fftOceanSimulator_->GetElapsedTime() : 0.0f);
            domain->PublishWaterSurfaceState(waterPlane_ ? &waterSurfaceState_ : nullptr);

            if (!waterPlane_) {
                break;
            }

            // WaterCVars（単一情報源）→ 各所への反映。Sync* より前に行い、
            // Sync* が読む WaterFrameConstants を最新化しておく
            ApplySettingsFromCVars(ctx, *domain);

            SyncCausticsAbsorption(*domain);
            SyncFoamSettings(*domain);

            const WaterFrameBinding binding = BuildFrameBinding(ctx, *domain);
            waterPlane_->ApplyFrameBinding(binding);
            LogFrameDiagnostics(binding);
            break;
        }
        default:
            break;
        }
    }

    void WaterRenderFeature::Finalize([[maybe_unused]] SceneContext& ctx)
    {
        // 水面オブジェクトは GameObjectManager が所有しているためポインタのみクリア。
        // ConnectSurfaceModelProvider は waterPlane_ の有無で接続/切断を決めるので、
        // 切断のためには **先に** null にしておく必要がある。
        waterPlane_ = nullptr;
        skyBox_ = nullptr;

        if (surfaceModelProvider_) {
            surfaceModelProvider_->ClearSurfaceData();
        }

        // publish を取り消す。waterSurfaceState_ は本 Feature の所有なので、
        // ここで外さないと次シーンの描画がダングリングポインタを読む。
        if (publishTarget_) {
            publishTarget_->PublishWaterSurfaceState(nullptr);
            publishTarget_->PublishFFTOceanSimulationTime(0.0f);
            ConnectSurfaceModelProvider(*publishTarget_);
            publishTarget_ = nullptr;
        }
    }

    float WaterRenderFeature::GetWaterHeight() const
    {
        return waterPlane_ ? waterPlane_->GetTransform().translate.y : 0.0f;
    }

    WaterSurfaceSimulator* WaterRenderFeature::GetActiveSimulator() const
    {
        if (!waterPlane_) {
            return nullptr;
        }
        return waterPlane_->IsUsingFFTOcean()
            ? fftOceanSimulator_.get()
            : gerstnerSimulator_.get();
    }

    void WaterRenderFeature::AcquireWaterPlane(SceneContext& ctx)
    {
        if (!ctx.gameObjectManager) {
            return;
        }

        // シーン側が既に水面を生成していればそれを採用する（EnvironmentFeature と同じ規約。
        // 具象型のダウンキャストではなく SceneTag で探す）
        if (auto* tag = ctx.gameObjectManager->FindFirstComponent<SceneTagComponent<WaterPlaneObject>>()) {
            waterPlane_ = tag->Get();
            return;
        }

        waterPlane_ = ctx.gameObjectManager->AddObject(
            std::make_unique<WaterPlaneObject>(config_.size, config_.resolution, config_.useFFTOcean));
        if (!waterPlane_) {
            return;
        }

        waterPlane_->GetTransform().translate = config_.translate;
        waterPlane_->GetTransform().scale = config_.scale;
        waterPlane_->SetBlendMode(BlendMode::kBlendModeNormal);
        // 既定のスクロール/タイリングは Lake プリセットを単一情報源とする
        // （以前はここと WaterPlaneObject コンストラクタに同値のハードコードが重複していた）
        const WaterPresetData& defaultPreset = GetWaterPresetData(WaterPresetType::Lake);
        waterPlane_->SetScrollSpeed(defaultPreset.scrollSpeed);
        waterPlane_->SetUVTiling(defaultPreset.uvTiling);
        waterPlane_->SetActive(true);
        ConfigureDefaultMaterial();
    }

    void WaterRenderFeature::ConfigureDefaultMaterial() const
    {
        auto* material = (waterPlane_ && waterPlane_->GetModel())
            ? waterPlane_->GetModel()->GetMaterial()
            : nullptr;
        if (!material) {
            return;
        }

        // 水面らしい鏡面的な PBR 初期値。Lake プリセット（WaterSurfaceTypes.h）が
        // 単一情報源（以前はここに複製があり roughness が 0.04 vs 0.03 で食い違っていた）
        const WaterPresetData& preset = GetWaterPresetData(WaterPresetType::Lake);
        material->SetColor(preset.baseColor);
        material->SetMetallic(preset.metallic);
        material->SetRoughness(preset.roughness);
        material->SetLightingEnabled(true);
        // 鏡面反射は RTWaterReflectionPass と空環境キューブマップで賄う。
        // ここで静的環境マップの IBL を効かせると大気の空と映り込みが食い違う。
        material->SetIBLIntensity(0.0f);
        material->SetNormalMapEnabled(false);
    }

    void WaterRenderFeature::ConnectSurfaceModelProvider(RenderDomainContext& domain) const
    {
        // 水面不在のフレームは provider を外し、RT 側をフォールバックへ切り替える
        const std::shared_ptr<const IWaterSurfaceModelProvider> provider =
            waterPlane_ ? std::static_pointer_cast<const IWaterSurfaceModelProvider>(surfaceModelProvider_)
            : std::shared_ptr<const IWaterSurfaceModelProvider>{};

        if (auto* refraction = domain.GetWaterRefractionRayTracingManager()) {
            refraction->SetSurfaceModelProvider(provider);
        }
        if (auto* caustics = domain.GetWaterCausticsRayTracingManager()) {
            caustics->SetSurfaceModelProvider(provider);
        }
        if (auto* reflection = domain.GetWaterReflectionRayTracingManager()) {
            reflection->SetSurfaceModelProvider(provider);
        }
    }

    WaterFrameBinding WaterRenderFeature::BuildFrameBinding(SceneContext& ctx, RenderDomainContext& domain) const
    {
        WaterFrameBinding binding{};

        // 水越しの背景色として使用するシーンカラー SRV
        if (auto* render = ctx.engine->GetService<Render>()) {
            if (auto* renderTargetManager = render->GetRenderTargetManager()) {
                RenderTarget* sceneColorTarget =
                    renderTargetManager->GetRenderTarget(RenderTargetNames::SceneColorSnapshot);
                if (!sceneColorTarget) {
                    sceneColorTarget = renderTargetManager->GetRenderTarget(RenderTargetNames::SceneColor);
                }
                if (sceneColorTarget) {
                    binding.resources.sceneColorSRV = sceneColorTarget->GetSRVHandle();
                }
            }
        }

        // Depth Fade 用のシーン深度 SRV
        if (auto* dxCommon = ctx.engine->GetService<DirectXCommon>()) {
            binding.resources.sceneDepthSRV = dxCommon->GetDepthStencilSRV();
        }

        // 深度線形化に使うカメラのクリップ距離。エディタ（far=100000 のデバッグカメラ）と
        // 実行時（既定 far=1000 のリリースカメラ）で値が変わるため毎フレーム同期する。
        if (ctx.gameViewCamera3D) {
            const CameraParameters cameraParams = ctx.gameViewCamera3D->GetParameters();
            binding.cameraNearZ = cameraParams.nearClip;
            binding.cameraFarZ = cameraParams.farClip;
        }

        // DXR 屈折結果のカラー SRV
        if (auto* refraction = domain.GetWaterRefractionRayTracingManager()) {
            binding.resources.refractionColorSRV = refraction->GetRefractionSRVHandle(
                WaterRefractionRayTracingManager::ViewID::GameView);
        }

        // DXR 反射結果を gReflectionTexture として接続する（鏡像カメラの置き換え）。
        // 反射源がシーン複雑度から切り離され、島の数に依存しない固定コストになる。
        if (auto* reflection = domain.GetWaterReflectionRayTracingManager()) {
            binding.resources.reflectionSRV = reflection->GetReflectionSRVHandle(
                WaterReflectionRayTracingManager::ViewID::GameView);
        }

        if (auto* fftOcean = domain.GetFFTOceanManager()) {
            binding.resources.SetFFTOceanTextureSRVs(
                fftOcean->GetDisplacementSRVHandle(),
                fftOcean->GetNormalSRVHandle(),
                fftOcean->GetJacobianSRVHandle(),
                fftOcean->GetFoamSRVHandle());
        }

        // 大気散乱（Aerial Perspective）・空アンビエント・空スペキュラの接続
        if (auto* atmosphere = domain.GetAtmosphereManager()) {
            const bool atmosphereSky = (skyBox_ != nullptr);
            const bool apEnabled = atmosphereSky
                && atmosphere->IsConstantBufferReady()
                && atmosphere->AreLUTsReady();

            binding.resources.atmosphereCB = atmosphere->GetConstantBufferGPUAddress();
            binding.resources.cameraVolumeSRV = atmosphere->GetCameraVolumeLUTSRVHandle();
            binding.resources.skyViewSRV = atmosphere->GetSkyViewLUTSRVHandle();
            binding.aerialPerspectiveEnabled = apEnabled;

            // 水中インスキャッタの天空光として Sky Irradiance SH を使う
            // （DeferredLighting の空アンビエントと同じソース・同じスケール）
            binding.resources.skyIrradianceSRV = atmosphere->GetSkyIrradianceSHSRVHandle();
            binding.skyAmbientScale = atmosphere->GetSkyAmbientScale();
            binding.skyAmbientEnabled = apEnabled
                && atmosphere->IsSkyAmbientEnabled()
                && atmosphere->IsSkyAmbientReady();

            // 空スペキュラキューブマップ（空＋雲）を反射への雲合成へ
            binding.resources.skyEnvironmentSRV = atmosphere->GetSkySpecularSRVHandle();
            binding.skyEnvReflectionEnabled = apEnabled
                && atmosphere->IsSkySpecularEnabled()
                && atmosphere->IsSkyEnvironmentReady();
        }

        return binding;
    }

    void WaterRenderFeature::ApplySettingsFromCVars(SceneContext& ctx, RenderDomainContext& domain)
    {
        if (!waterPlane_) {
            return;
        }

        // ---- 見た目・水質・泡 → WaterPlaneObject ----
        // setter は CPU 側ミラーの更新のみで安価なため毎フレーム呼んでよい
        // （cbuffer 転送は描画時に一括で行われる）。
        waterPlane_->SetBaseColor(WaterCVars::BaseColor.Get());
        waterPlane_->SetRoughness(WaterCVars::Roughness.Get());
        waterPlane_->SetMetallic(WaterCVars::Metallic.Get());
        waterPlane_->SetIBLEnabled(WaterCVars::IBLEnabled.Get());
        waterPlane_->SetFresnelParameters(WaterCVars::FresnelScale.Get(), WaterCVars::FresnelF0.Get());
        waterPlane_->SetScrollSpeed(WaterCVars::ScrollSpeed.Get());
        waterPlane_->SetUVTiling(WaterCVars::UVTiling.Get());
        waterPlane_->SetDepthFade(WaterCVars::DepthFadeEnabled.Get());
        // 実効 σa/σs = ベース + 濁度ゲイン（合成は WaterCVars 側。永続化はベース値のみ）
        waterPlane_->SetWaterOpticalCoefficients(
            WaterCVars::EffectiveAbsorption(), WaterCVars::EffectiveScattering());
        // 白波被覆率の風速追従係数。変化したときだけログして、
        // 「風速を変えたのに泡が追従していない」を目視でなく数値で追えるようにする。
        const float foamWindCoverageScale =
            WaterPlaneObject::ComputeFoamWindCoverageScale(WaterCVars::FFTWindSpeed.Get());
        if (std::abs(foamWindCoverageScale - lastFoamWindCoverageScale_) > 1.0e-4f) {
            lastFoamWindCoverageScale_ = foamWindCoverageScale;
            Logger::GetInstance().Infof(
                LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterRenderFeature: foam wind coverage scale = {:.5f} (windSpeed={:.2f}, Monahan U^3.41 / ref 18m/s)",
                foamWindCoverageScale, WaterCVars::FFTWindSpeed.Get());
        }

        waterPlane_->SetFoamParameters(
            WaterCVars::FoamEnabled.Get(),
            WaterCVars::FoamBias.Get(),
            WaterCVars::FoamGain.Get(),
            WaterCVars::FoamOpacity.Get(),
            WaterCVars::FoamCascadeWeights.Get(),
            WaterCVars::FoamDecaySeconds.Get(),
            // 白波の量は風速へ自動追従させる（FoamBias は触らない）。
            // これが無いと、ある風速で合わせた Bias が別の風速で必ず破綻する。
            foamWindCoverageScale);

        // ---- FFT Ocean 経路の有効/無効（変化時のみ。PSO 再構築を伴う）----
        const bool fftEnabled = WaterCVars::FFTEnabled.Get();
        if (waterPlane_->IsUsingFFTOcean() != fftEnabled) {
            waterPlane_->SetUseFFTOcean(fftEnabled);
        }

        // ---- FFT シミュレーション設定（revision 変化時のみ）----
        if (auto* fftOcean = domain.GetFFTOceanManager()) {
            const uint32_t fftRevision =
                WaterCVars::FFTAmplitudeScale.GetRevision()
                + WaterCVars::FFTWindDirection.GetRevision()
                + WaterCVars::FFTWindSpeed.GetRevision()
                + WaterCVars::FFTFetchKilometers.GetRevision()
                + WaterCVars::FFTChoppiness.GetRevision()
                + WaterCVars::FFTActiveComponentCount.GetRevision()
                + WaterCVars::FFTGravity.GetRevision()
                + WaterCVars::SwellEnabled.GetRevision()
                + WaterCVars::SwellHeight.GetRevision()
                + WaterCVars::SwellPeriod.GetRevision()
                + WaterCVars::SwellDirection.GetRevision()
                + WaterCVars::SwellRelativeWidth.GetRevision()
                + WaterCVars::SwellSpread.GetRevision();
            if (fftRevision != lastFFTCVarRevisionSum_) {
                lastFFTCVarRevisionSum_ = fftRevision;
                FFTOceanManager::Settings settings = fftOcean->GetSettings();
                settings.amplitudeScale = WaterCVars::FFTAmplitudeScale.Get();
                settings.windDirection[0] = WaterCVars::FFTWindDirection.Get().x;
                settings.windDirection[1] = WaterCVars::FFTWindDirection.Get().y;
                settings.windSpeed = WaterCVars::FFTWindSpeed.Get();
                settings.fetchMeters = WaterCVars::FFTFetchKilometers.Get() * 1000.0f;
                settings.choppiness = WaterCVars::FFTChoppiness.Get();
                settings.activeComponentCount =
                    static_cast<uint32_t>((std::max)(WaterCVars::FFTActiveComponentCount.Get(), 1));
                settings.gravity = WaterCVars::FFTGravity.Get();
                settings.swellEnabled = WaterCVars::SwellEnabled.Get();
                settings.swellHeightMeters = WaterCVars::SwellHeight.Get();
                settings.swellPeriodSeconds = WaterCVars::SwellPeriod.Get();
                settings.swellDirection[0] = WaterCVars::SwellDirection.Get().x;
                settings.swellDirection[1] = WaterCVars::SwellDirection.Get().y;
                settings.swellRelativeWidth = WaterCVars::SwellRelativeWidth.Get();
                settings.swellSpreadExponent = WaterCVars::SwellSpread.Get();
                // 変更検知・サニタイズ・WaitForPreviousFrame・スペクトル再構築は SetSettings が担う
                fftOcean->SetSettings(settings);
            }
        }

        // ---- コースティクス（見た目パラメータ。debug 系フィールドは UI 側の担当を維持）----
        if (auto* techniqueManager = ctx.engine ? ctx.engine->GetService<RenderingTechniqueManager>() : nullptr) {
            if (auto* caustics = techniqueManager->GetTechnique<WaterCausticsTechnique>(
                    RenderingTechniqueNames::WaterCaustics)) {
                WaterCausticsTechnique::Params params = caustics->GetParams();
                params.intensity = WaterCVars::CausticsIntensity.Get();
                params.depthAttenuation = WaterCVars::CausticsDepthAttenuation.Get();
                params.curvatureScale = WaterCVars::CausticsCurvatureScale.Get();
                params.surfaceSampleRadius = WaterCVars::CausticsSurfaceSampleRadius.Get();
                params.refractiveIndex = WaterCVars::CausticsRefractiveIndex.Get();
                params.receiverNormalStrength = WaterCVars::CausticsReceiverNormalStrength.Get();
                params.alignmentPower = WaterCVars::CausticsAlignmentPower.Get();
                caustics->SetParams(params);
                caustics->SetBackend(WaterCVars::CausticsBackend.Get() == 1
                    ? WaterCausticsTechnique::Backend::ScreenSpace
                    : WaterCausticsTechnique::Backend::RayTracing);
            }
        }
        if (auto* rtCaustics = domain.GetWaterCausticsRayTracingManager()) {
            // 屈折率の真実の源は CVar（以前は technique と RT の 2 箇所が食い違い得た）。
            // absorptionCoeff は SyncCausticsAbsorption が WaterFrameConstants から同期する
            WaterCausticsRayTracingSettings settings = rtCaustics->GetSettings();
            settings.refractiveIndex = WaterCVars::CausticsRefractiveIndex.Get();
            rtCaustics->SetSettings(settings);
        }

        // ---- DXR 屈折 ----
        if (auto* rtRefraction = domain.GetWaterRefractionRayTracingManager()) {
            WaterRefractionRayTracingSettings settings = rtRefraction->GetSettings();
            settings.maxRefractionOffsetPixels = WaterCVars::RefractionMaxOffsetPixels.Get();
            rtRefraction->SetSettings(settings);
        }
    }

    void WaterRenderFeature::SyncFoamSettings(RenderDomainContext& domain) const
    {
        // 泡パラメータの単一情報源は WaterFrameConstants（UI / 永続化と同経路）。
        // 蓄積パス（FFTOceanFoamAccumulate.CS）が使う分を毎フレーム転送する。
        auto* fftOcean = domain.GetFFTOceanManager();
        if (!fftOcean || !waterPlane_) {
            return;
        }

        const WaterFrameConstants& frameConstants = waterPlane_->GetFrameConstants();
        FFTOceanManager::FoamSettings foamSettings{};
        foamSettings.enabled = frameConstants.foamEnabled != 0 && waterPlane_->IsUsingFFTOcean();
        foamSettings.bias = frameConstants.foamBias;
        foamSettings.gain = frameConstants.foamGain;
        foamSettings.cascadeWeights[0] = frameConstants.foamCascadeWeights[0];
        foamSettings.cascadeWeights[1] = frameConstants.foamCascadeWeights[1];
        foamSettings.cascadeWeights[2] = frameConstants.foamCascadeWeights[2];
        foamSettings.decaySeconds = frameConstants.foamDecaySeconds;
        fftOcean->SetFoamSettings(foamSettings);
    }

    void WaterRenderFeature::SyncCausticsAbsorption(RenderDomainContext& domain) const
    {
        // 水面描画と同じ波長依存吸収係数 σa を RT コースティクスへ同期する
        // （Jerlov プリセット / 濁度 UI の変更に Beer–Lambert 減衰を追従させる）
        auto* caustics = domain.GetWaterCausticsRayTracingManager();
        if (!caustics || !waterPlane_) {
            return;
        }

        const WaterFrameConstants& frameConstants = waterPlane_->GetFrameConstants();
        WaterCausticsRayTracingSettings settings = caustics->GetSettings();
        settings.absorptionCoeff[0] = frameConstants.absorptionCoeff[0];
        settings.absorptionCoeff[1] = frameConstants.absorptionCoeff[1];
        settings.absorptionCoeff[2] = frameConstants.absorptionCoeff[2];
        caustics->SetSettings(settings);
    }

    void WaterRenderFeature::RefreshWaterSurfaceState()
    {
        // 前フレームの値を破棄し、現在の水面状態から再構築する
        waterSurfaceState_ = {};
        if (!waterPlane_) {
            return;
        }

        // 水面オブジェクトが非表示のフレームは「水なし」として扱う。
        // ここで regionValid=0 のゼロ値を publish しないと、RT コースティクスの
        // ディスパッチと DeferredLighting の水中ライティング（直接光のコースティクス置換・
        // アンビエントの Beer–Lambert 減衰）が動き続け、非表示のはずの水の光学効果
        // （薄い青色）が床に乗り続ける。
        if (!waterPlane_->IsActive()) {
            if (surfaceModelProvider_) {
                surfaceModelProvider_->ClearSurfaceData();
            }
            return;
        }

        WaterSurfaceSimulationInput simulationInput{};
        simulationInput.waterHeight = waterPlane_->GetTransform().translate.y;
        simulationInput.gerstnerConstants = &waterPlane_->GetWaterConstants();

        auto* activeSimulator = GetActiveSimulator();
        if (!activeSimulator) {
            // simulator 不在時も provider の参照先を維持し、
            // RT 側がゼロ初期化データへ安全にフォールバックできるようにする
            if (surfaceModelProvider_) {
                surfaceModelProvider_->SetSurfaceData(
                    waterSurfaceState_, WaterSurfaceSimulationType::Gerstner);
            }
            return;
        }

        activeSimulator->CaptureSurface(simulationInput, waterSurfaceState_);

        // 水面メッシュのワールド XZ 範囲。コースティクスが水域の外（無限市松床など）へ
        // 漏れないようにするための受光マスク。メッシュはローカル ±localSize/2 に広がる
        // 正方形（回転は非対応＝ゼロ前提）なので、ワールド半径は 0.5 * localSize * scale。
        const float localSize = waterPlane_->GetSize();
        if (localSize > 1.0e-4f) {
            const auto& transform = waterPlane_->GetTransform();
            waterSurfaceState_.regionCenterXZ[0] = transform.translate.x;
            waterSurfaceState_.regionCenterXZ[1] = transform.translate.z;
            waterSurfaceState_.regionHalfExtentXZ[0] = 0.5f * localSize * transform.scale.x;
            waterSurfaceState_.regionHalfExtentXZ[1] = 0.5f * localSize * transform.scale.z;
            waterSurfaceState_.regionValid = 1;
        }
        // coverage 判定のメッシュ同一基準化に使う実際の頂点グリッド分割数
        waterSurfaceState_.meshSubdivisions = static_cast<float>(waterPlane_->GetResolution());

        if (surfaceModelProvider_) {
            surfaceModelProvider_->SetSurfaceData(
                waterSurfaceState_, activeSimulator->GetSimulationType());
        }
    }

    void WaterRenderFeature::LogFrameDiagnostics(const WaterFrameBinding& binding) const
    {
        // デバッグ表示中のみ。以前は SRV setter 6 個・BindCustomResources・
        // サーフェス更新に散らばった 8 種類のログがそれぞれ独自の頻度で出ていた。
        if (!waterPlane_ || waterPlane_->GetFrameConstants().depthFadeDebugEnabled == 0) {
            return;
        }

        static uint32_t sLogCounter = 0u;
        if ((sLogCounter++ % kDiagnosticsLogInterval) != 0u) {
            return;
        }

        const WaterFrameConstants& frameConstants = waterPlane_->GetFrameConstants();
        const WaterRenderResources& resources = binding.resources;
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::RenderTarget,
            "WaterRenderFeature: SRV sceneColor=0x{:X} sceneDepth=0x{:X} refraction=0x{:X} reflection=0x{:X} "
            "fftDisp=0x{:X} fftNormal=0x{:X} skyIrradiance=0x{:X} skyEnv=0x{:X} | "
            "flags refl={} ap={} skyAmbient={} skyEnv={} debugMode={} | "
            "clip near={:.3f} far={:.1f} | surface height={:.3f} waves={} time={:.3f} regionValid={} | useFFT={}",
            resources.sceneColorSRV.ptr,
            resources.sceneDepthSRV.ptr,
            resources.refractionColorSRV.ptr,
            resources.reflectionSRV.ptr,
            resources.fftDisplacementSRV.ptr,
            resources.fftNormalSRV.ptr,
            resources.skyIrradianceSRV.ptr,
            resources.skyEnvironmentSRV.ptr,
            frameConstants.reflectionEnabled,
            frameConstants.aerialPerspectiveEnabled,
            frameConstants.skyAmbientEnabled,
            frameConstants.skyEnvReflectionEnabled,
            frameConstants.depthDebugViewMode,
            frameConstants.cameraNearZ,
            frameConstants.cameraFarZ,
            waterSurfaceState_.waterHeight,
            waterSurfaceState_.activeWaveCount,
            waterSurfaceState_.time,
            waterSurfaceState_.regionValid,
            waterPlane_->IsUsingFFTOcean());
    }
}
