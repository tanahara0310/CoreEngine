#include "pch.h"
#include "WaterSystem.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Render/Model/VertexAnimation.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/Query/WaterHeightQueryManager.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/Surface/WaterSurfaceComponent.h"
#include "Graphics/Water/System/WaterFrameContext.h"
#include "Graphics/Water/WaterProfile.h"
#include "Scene/Feature/SceneFeatureRegistry.h"
#ifdef CORE_EDITOR
#include "Editor/Water/WaterEditorPanel.h"
#endif
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

SCENE_FEATURE_REGISTER("WaterRender", [] { return std::make_unique<CoreEngine::WaterSystem>(); })

namespace CoreEngine
{
    namespace {
        /// @brief 診断ログの出力間隔（フレーム）
        constexpr uint32_t kDiagnosticsLogInterval = 120u;

        /// @brief 水面を描いているか（コンポーネントとオブジェクトの両方が有効）
        bool IsWaterActive(const WaterSurfaceComponent& waterPlane)
        {
            const GameObject* const owner = waterPlane.GetOwner();
            return waterPlane.IsEnabled() && owner && owner->IsActive();
        }
    }

    WaterSystem::WaterSystem(const Config& config)
        : config_(config) {
    }

    WaterSystem::~WaterSystem() = default;

    void WaterSystem::Initialize(SceneContext& ctx)
    {
        // 水面の描画に使うマネージャ（水面のレイトレ・FFT の海・泡）は、水面のあるシーンで初めて作る
        RenderDomainContext* const domain = ctx.engine ? ctx.engine->GetRenderDomainContext() : nullptr;
        if (domain) {
            domain->EnsureWaterInitialized();
        }

        AcquireWaterPlane(ctx);
        const WaterSettings& settings = RefreshSettings();
        if (domain) {
            // FFT の海のスペクトルは設定が変わったときだけ作り直す
            wave_.ApplyModelSettings(*domain, settings.waves);
        }

#ifdef CORE_EDITOR
        // 水面の調整画面は水面と一緒に出す（シーンのコードは登録に関与しない）
        if (!editorPanel_) {
            editorPanel_ = std::make_unique<WaterEditorPanel>();
        }
        if (ctx.engine) {
            editorPanel_->Initialize(this, *ctx.engine);
        }
#endif

        // 初期フレームでも水面シェーダーの時間が不定にならないよう即時反映する
        if (WaterSurfaceComponent* const waterPlane = waterPlane_.Get()) {
            waterPlane->SetSimulationTime(wave_.GetDrawnModel(*waterPlane).GetTime());
        }

        publishTarget_ = domain;
        if (domain) {
            CaptureSnapshot(*domain, settings, 0.0f);
        }
    }

    void WaterSystem::PostSceneInitialize(SceneContext& ctx)
    {
        if (!ctx.gameObjectManager) {
            return;
        }

        AdoptSceneWaterPlane(ctx);
        appearance_.FindSky(ctx);

        // 保存データの水面に乗り換えたら、その水の設定でスペクトルを作っておく
        if (RenderDomainContext* const domain = ctx.engine ? ctx.engine->GetRenderDomainContext() : nullptr) {
            wave_.ApplyModelSettings(*domain, RefreshSettings().waves);
        }
    }

    void WaterSystem::AdoptSceneWaterPlane(SceneContext& ctx)
    {
        // 保存データで置いた水面があれば、そちらを使ってこの Feature が作った水面は消す
        WaterSurfaceComponent* const ownWaterPlane = waterPlane_.Get();
        if (!ownsWaterPlane_ || !ownWaterPlane) {
            return;
        }

        WaterSurfaceComponent* sceneWaterPlane = nullptr;
        ctx.gameObjectManager->ForEachComponent<WaterSurfaceComponent>(
            [&](WaterSurfaceComponent& component) {
                if (&component != ownWaterPlane) {
                    sceneWaterPlane = &component;
                }
            });
        if (!sceneWaterPlane) {
            return;
        }

        if (GameObject* const owner = ownWaterPlane->GetOwner()) {
            owner->Destroy();
        }
        waterPlane_.Set(sceneWaterPlane);
        ownsWaterPlane_ = false;
    }

    void WaterSystem::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        switch (phase) {
        case SceneUpdatePhase::PreObjectUpdate:
        {
            // 波の時刻を進め、描いているモデルの時刻を水面へ渡す
            WaterSurfaceComponent* const waterPlane = waterPlane_.Get();
            wave_.Advance(Time::DeltaTime(), waterPlane);
            if (waterPlane) {
                waterPlane->SetSimulationTime(wave_.GetDrawnModel(*waterPlane).GetTime());
            }

            // GPU が済んだ水面の高さの問い合わせを、オブジェクトの更新より前に受け取る
            RenderDomainContext* const domain = ctx.engine ? ctx.engine->GetRenderDomainContext() : nullptr;
            if (WaterHeightQueryManager* const query = domain ? domain->GetWaterHeightQueryManager() : nullptr) {
                query->CollectResults();
            }
            break;
        }
        case SceneUpdatePhase::PostLogic:
        {
            RenderDomainContext* const domain = ctx.engine ? ctx.engine->GetRenderDomainContext() : nullptr;
            if (!domain) {
                break;
            }
            publishTarget_ = domain;

            const WaterSettings& settings = RefreshSettings();
            WaterSurfaceComponent* const waterPlane = waterPlane_.Get();
            if (!waterPlane) {
                VertexAnimationParams::SetSwellDirection(std::nullopt);
                CaptureSnapshot(*domain, settings, Time::DeltaTime());
                domain->PublishWaterFrame(nullptr, wave_.GetFFTOceanTime());
                break;
            }

            // 潮 → 設定 → 海底の範囲（スナップショットの前に水面の高さ・波のモデル・岸の範囲を決める）
            const LightManager* const lightManager = ctx.engine ? ctx.engine->GetService<LightManager>() : nullptr;
            tide_.Update(lightManager, *waterPlane);

            // 海草はこの水のうねりの向きへ寄せ返す
            VertexAnimationParams::SetSwellDirection(settings.waves.swellEnabled
                ? std::optional<Vector2>(settings.waves.swellDirection) : std::nullopt);

            const WaterFrameContext frame{ ctx, *domain, *waterPlane, settings };
            wave_.ApplySettings(frame);
            const WaterWaveCapabilities waveCapabilities = wave_.GetDrawnModel(*waterPlane).GetCapabilities();
            appearance_.ApplySettings(frame);
            foam_.ApplySettings(frame, waveCapabilities, wave_.GetFFTOceanTime());
            caustics_.ApplySettings(frame);
            rayTracing_.ApplySettings(frame);
            shore_.UpdateWindow(frame, waveCapabilities);

            CaptureSnapshot(*domain, settings, Time::DeltaTime());
            domain->PublishWaterFrame(&snapshot_, wave_.GetFFTOceanTime());

            // 大気散乱の更新後なので、LUT・SH・キューブマップの準備完了はこの時点で確定している
            const WaterFrameBinding binding = BuildFrameBinding(frame);
            waterPlane->ApplyFrameBinding(binding);
            LogFrameDiagnostics(binding);
            break;
        }
        default:
            break;
        }
    }

    void WaterSystem::Finalize([[maybe_unused]] SceneContext& ctx)
    {
#ifdef CORE_EDITOR
        // 調整画面のドロワーが Feature より長生きしないよう、先に登録を外す
        if (editorPanel_) {
            editorPanel_->Shutdown();
        }
#endif

        // 水面オブジェクトは GameObjectManager が所有しているため参照だけ外す
        waterPlane_.Reset();
        appearance_.Reset();
        VertexAnimationParams::SetSwellDirection(std::nullopt);

        // snapshot_ はこの Feature の持ち物なので、受け渡しを取り消さないと次のシーンの描画が消えた実体を読む
        if (publishTarget_) {
            publishTarget_->PublishWaterFrame(nullptr, 0.0f);
            publishTarget_ = nullptr;
        }
    }

    const WaterFoamSystem* WaterSystem::GetWaterFoamSystem() const
    {
        return publishTarget_ ? publishTarget_->GetWaterFoamSystem() : nullptr;
    }

    WaterSurfaceComponent* WaterSystem::GetWaterPlane() const
    {
        return waterPlane_.Get();
    }

    float WaterSystem::GetWaterHeight() const
    {
        const WaterSurfaceComponent* const waterPlane = waterPlane_.Get();
        return waterPlane ? waterPlane->GetWaterLevel() : 0.0f;
    }

    void WaterSystem::AcquireWaterPlane(SceneContext& ctx)
    {
        if (!ctx.gameObjectManager) {
            return;
        }

        // シーンのコードが既に水面を置いていればそれを採用する（EnvironmentFeature と同じ規約）。
        // 保存データで置いた水面はこの時点ではまだ生まれていないので、PostSceneInitialize で見直す
        if (auto* existing = ctx.gameObjectManager->FindFirstComponent<WaterSurfaceComponent>()) {
            waterPlane_.Set(existing);
            return;
        }

        auto owned = std::make_unique<GameObject>();
        owned->SetName("WaterPlane");
        GameObject* const object = ctx.gameObjectManager->AddObject(std::move(owned));
        if (!object) {
            return;
        }
        WaterSurfaceComponent* const waterPlane = object->AddComponent<WaterSurfaceComponent>(
            config_.size, config_.resolution);
        if (!waterPlane) {
            return;
        }
        waterPlane_.Set(waterPlane);
        ownsWaterPlane_ = true;

        if (TransformComponent* const transform = object->GetComponent<TransformComponent>()) {
            transform->SetTranslate(config_.translate);
            transform->SetScale(config_.scale);
        }
        ConfigureDefaultMaterial();
    }

    void WaterSystem::ConfigureDefaultMaterial() const
    {
        WaterSurfaceComponent* const waterPlane = waterPlane_.Get();
        auto* material = (waterPlane && waterPlane->GetModel())
            ? waterPlane->GetModel()->GetMaterial()
            : nullptr;
        if (!material) {
            return;
        }

        // 水面らしい鏡面的な PBR 初期値（Lake プリセットの色と粗さ）
        const WaterPresetData& preset = GetWaterPresetData(WaterPresetType::Lake);
        material->SetColor(preset.baseColor);
        material->SetMetallic(0.0f);
        material->SetRoughness(preset.roughness);
        material->SetLightingEnabled(true);
        material->SetNormalMapEnabled(false);
    }

    const WaterSettings& WaterSystem::RefreshSettings()
    {
        const WaterSurfaceComponent* const waterPlane = waterPlane_.Get();
        settings_ = BuildWaterSettings(waterPlane ? waterPlane->GetProfile() : WaterProfile{});
        return settings_;
    }

    void WaterSystem::CaptureSnapshot(RenderDomainContext& domain, const WaterSettings& settings, float deltaSeconds)
    {
        snapshot_ = {};
        WaterSurfaceComponent* const waterPlane = waterPlane_.Get();
        if (!waterPlane) {
            wetness_.Clear();
            return;
        }

        // 水面が無効なフレームは regionValid=0 の「水なし」にする（RT コースティクスと水中の光を止める）
        if (IsWaterActive(*waterPlane)) {
            wave_.Capture(domain, *waterPlane, snapshot_);
            snapshot_.tideHeight = waterPlane->GetTideHeight();

            // 水面メッシュのワールド XZ 範囲（コースティクスを水域の外へ漏らさない受光マスク）。
            // メッシュはローカル ±size/2 の正方形で、回転は扱わない
            const float localSize = waterPlane->GetSize();
            if (localSize > 1.0e-4f) {
                const auto& transform = waterPlane->GetTransform();
                snapshot_.regionCenterXZ[0] = transform.GetTranslate().x;
                snapshot_.regionCenterXZ[1] = transform.GetTranslate().z;
                snapshot_.regionHalfExtentXZ[0] = 0.5f * localSize * transform.GetScale().x;
                snapshot_.regionHalfExtentXZ[1] = 0.5f * localSize * transform.GetScale().z;
                snapshot_.regionValid = 1;
            }
            // 被覆の判定をメッシュと同じ基準にするための頂点の分割数
            snapshot_.meshSubdivisions = static_cast<float>(waterPlane->GetResolution());

            seaMap_.Capture(domain, settings, wave_.GetDrawnModel(*waterPlane), snapshot_);
            wetness_.Capture(settings, deltaSeconds, snapshot_);
        } else {
            wetness_.Clear();
        }

        // レイトレの水面も、ラスタと同じ岸の細かい格子で描く
        shore_.Capture(*waterPlane, snapshot_);
    }

    WaterFrameBinding WaterSystem::BuildFrameBinding(const WaterFrameContext& frame) const
    {
        WaterFrameBinding binding{};
        appearance_.Bind(frame, binding);
        rayTracing_.Bind(frame.domain, binding);
        shore_.Bind(binding);
        seaMap_.Bind(frame.domain, snapshot_, binding);
        wave_.Bind(frame.domain, binding);
        foam_.Bind(frame.domain, binding);
        return binding;
    }

    void WaterSystem::LogFrameDiagnostics(const WaterFrameBinding& binding) const
    {
        WaterSurfaceComponent* const waterPlane = waterPlane_.Get();
        if (!waterPlane || waterPlane->GetFrameConstants().depthFadeDebugEnabled == 0) {
            return;
        }

        static uint32_t sLogCounter = 0u;
        if ((sLogCounter++ % kDiagnosticsLogInterval) != 0u) {
            return;
        }

        const WaterFrameConstants& frameConstants = waterPlane->GetFrameConstants();
        const WaterRenderResources& resources = binding.resources;
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::RenderTarget,
            "WaterSystem: SRV sceneColor=0x{:X} sceneDepth=0x{:X} refraction=0x{:X} reflection=0x{:X} "
            "fftDisp=0x{:X} fftNormal=0x{:X} skyIrradiance=0x{:X} skyEnv=0x{:X} | "
            "flags refl={} ap={} skyAmbient={} skyEnv={} debugMode={} | "
            "clip near={:.3f} far={:.1f} | surface height={:.3f} waves={} time={:.3f} regionValid={} | waveModel={}",
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
            snapshot_.waterHeight,
            snapshot_.activeWaveCount,
            snapshot_.time,
            snapshot_.regionValid,
            static_cast<uint32_t>(waterPlane->GetWaveModel()));
    }
}
