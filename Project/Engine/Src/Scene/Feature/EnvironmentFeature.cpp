#include "pch.h"
#include "EnvironmentFeature.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/PlaybackState.h"
#include "Camera/Camera.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/GameObject.h"
#include "Graphics/Render/SkyBox/SkyBoxComponent.h"
#include "GameObject/Component/Environment/VolumetricCloudComponent.h"
#include "GameObject/Component/Environment/HeightFogComponent.h"
#include "GameObject/Component/Environment/PostProcessComponent.h"
#include "Graphics/Cloud/Settings/CloudCVars.h"
#include "Graphics/Fog/Settings/FogCVars.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Fog/FogManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Scene/SceneEnvironmentIO.h"
#include "Scene/SceneSaveSystem.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    void EnvironmentFeature::PostSceneInitialize(SceneContext& ctx)
    {
        // シーンが置いていないものだけを自動生成するため、オブジェクトが出そろった後に行う
        SetupEnvironmentObject(ctx);

#ifdef CORE_EDITOR
        // 復元直後の通番を基準にする（そろえないと、開いた直後に読んだ値をそのまま書き戻す）
        lastEnvironmentRevision_ = SceneEnvironmentIO::GetChangeRevision();
#endif
    }

    void EnvironmentFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        switch (phase) {
        case SceneUpdatePhase::PostLogic:
            // インスペクタのチェックと CVar をそろえてから描画側へ渡す
            SyncComponentToggles();
            // 大気散乱の更新（全ロジック更新後の最新の太陽・カメラ情報を反映する）
            UpdateAtmosphere(ctx);
            // フォグは空・大気の有無に依存しないので、大気更新の成否と無関係に呼ぶ
            UpdateFog(ctx);
#ifdef CORE_EDITOR
            AutoSaveEnvironment(ctx);
#endif
            break;
        default:
            break;
        }
    }

    void EnvironmentFeature::Finalize([[maybe_unused]] SceneContext& ctx)
    {
#ifdef CORE_EDITOR
        // 書き待ちのまま閉じると最後の調整が消えるので、ここで書き切る
        if (environmentDirty_ && ctx.saveSystem) {
            environmentDirty_ = false;
            SceneEnvironmentIO::Save(ctx.saveSystem->GetSceneName());
        }
#endif

        // どれも GameObjectManager が所有しているためポインタのみクリア
        skyBox_ = nullptr;
        cloud_ = nullptr;
        fog_ = nullptr;
        postProcess_ = nullptr;
    }

#ifdef CORE_EDITOR
    void EnvironmentFeature::AutoSaveEnvironment(SceneContext& ctx)
    {
        if (!ctx.saveSystem) {
            return;
        }

        // 再生中に触った分はシーンへ書かない。停止すると再生前のシーンへ組み直すので、
        // 環境だけがファイルに残ると「停止で戻す」と食い違う。
        // 通番は追いかけておき、再生前との差だけを見る
        if (PlaybackStateManager::GetInstance().IsInPlayMode()) {
            lastEnvironmentRevision_ = SceneEnvironmentIO::GetChangeRevision();
            environmentDirty_ = false;
            return;
        }

        const uint64_t revision = SceneEnvironmentIO::GetChangeRevision();
        if (revision != lastEnvironmentRevision_) {
            lastEnvironmentRevision_ = revision;
            lastEnvironmentChange_ = std::chrono::steady_clock::now();
            environmentDirty_ = true;
            return;
        }
        if (!environmentDirty_) {
            return;
        }

        // 動かしている間は書かない（離してから 0.3 秒で 1 回だけ書く）
        constexpr auto kQuietTime = std::chrono::milliseconds(300);
        if (std::chrono::steady_clock::now() - lastEnvironmentChange_ < kQuietTime) {
            return;
        }

        environmentDirty_ = false;
        SceneEnvironmentIO::Save(ctx.saveSystem->GetSceneName());
    }
#endif

    void EnvironmentFeature::SetupEnvironmentObject(SceneContext& ctx)
    {
        GameObjectManager* const objects = ctx.gameObjectManager;
        if (!objects) {
            return;
        }

        // シーン側が置いた分をまず採る
        skyBox_ = objects->FindFirstComponent<SkyBoxComponent>();
        cloud_ = objects->FindFirstComponent<VolumetricCloudComponent>();
        fog_ = objects->FindFirstComponent<HeightFogComponent>();
        postProcess_ = objects->FindFirstComponent<PostProcessComponent>();
        if (skyBox_ && cloud_ && fog_ && postProcess_) {
            Logger::GetInstance().Infof(LogCategory::System,
                "EnvironmentFeature: シーンが置いた環境を採用");
            SyncComponentToggles();
            return;
        }

        // 足りない分を載せる入れ物を用意する（シーンには保存しない）
        GameObject* host = skyBox_ ? skyBox_->GetOwner() : nullptr;
        if (!host) {
            auto owned = std::make_unique<GameObject>();
            owned->SetName("Environment");
            host = objects->AddObject(std::move(owned));
            if (!host) {
                return;
            }
            host->SetSerializeEnabled(false);
        }

        if (!skyBox_) { skyBox_ = host->AddComponent<SkyBoxComponent>(); }
        if (!cloud_) { cloud_ = host->AddComponent<VolumetricCloudComponent>(); }
        if (!fog_) { fog_ = host->AddComponent<HeightFogComponent>(); }
        if (!postProcess_) { postProcess_ = host->AddComponent<PostProcessComponent>(); }

        // 実体の値（CVar）に合わせてチェックの初期状態を決める
        if (cloud_) { cloud_->SetEnabled(CloudCVars::Enabled.Get()); }
        if (fog_) { fog_->SetEnabled(FogCVars::Enabled.Get()); }
        lastCloudEnabled_ = CloudCVars::Enabled.Get();
        lastFogEnabled_ = FogCVars::Enabled.Get();

        Logger::GetInstance().Infof(LogCategory::System,
            "EnvironmentFeature: 既定の環境（空・雲・霧・ポストエフェクト）を {} に載せた",
            host->GetName());
    }

    void EnvironmentFeature::SyncComponentToggles()
    {
        const auto sync = [](IComponent* component, CVar<bool>& cvar, bool& last) {
            if (!component) {
                return;
            }
            if (cvar.Get() != last) {
                // CVar パネルやコンソールから変わった
                component->SetEnabled(cvar.Get());
            } else if (component->IsEnabled() != cvar.Get()) {
                // インスペクタのチェックから変わった
                cvar.Set(component->IsEnabled());
            }
            last = cvar.Get();
            };
        sync(cloud_, CloudCVars::Enabled, lastCloudEnabled_);
        sync(fog_, FogCVars::Enabled, lastFogEnabled_);
    }

    void EnvironmentFeature::UpdateAtmosphere(SceneContext& ctx)
    {
        // 空（SkyBox）が無いシーンでは AtmosphereManager を非アクティブのままにし、
        // LUT 生成・Aerial Perspective 合成をスキップさせる
        if (!skyBox_) {
            return;
        }

        auto* domainContext = ctx.engine->GetRenderDomainContext();
        auto* atmosphereManager = domainContext ? domainContext->GetAtmosphereManager() : nullptr;
        if (!atmosphereManager) {
            return;
        }

        // ゲームビューカメラ基準で太陽情報とカメラ高度を反映する
        Vector3 cameraPosition{};
        Matrix4x4 viewMatrix = MathCore::Matrix::Identity();
        Matrix4x4 projMatrix = MathCore::Matrix::Identity();
        if (const Camera* camera = ctx.gameViewCamera3D) {
            cameraPosition = camera->GetPosition();
            viewMatrix = camera->GetViewMatrix();
            projMatrix = camera->GetProjectionMatrix();
        }
        atmosphereManager->Update(cameraPosition, viewMatrix, projMatrix,
                                  ctx.engine->GetService<LightManager>());

        // 照明駆動露出: 大気が解析した「シーン照明の代表輝度」を ToneMapping へ毎フレーム供給する。
        // カメラの向き（画面の構図）に露出が影響されなくなる（供給が無いシーンは画面平均測光へ
        // 自動フォールバックするため、大気非対応シーンではこの呼び出し自体が無くてよい）
        if (auto* postEffect = ctx.engine->GetService<PostEffectManager>()) {
            if (auto* toneMapping = postEffect->GetEffect<ToneMapping>(PostEffectNames::ToneMapping)) {
                toneMapping->SetSceneIlluminationLuminance(
                    atmosphereManager->GetSceneIlluminationLuminance());
            }
        }

        // 大気散乱の直後に雲を更新する（大気モード時のみ、という既存ガードの内側なので追加ガード不要）。
        // 雲は太陽情報・カメラ高度を AtmosphereManager から取得するため、大気 Update の後に呼ぶ。
        if (auto* cloudManager = domainContext->GetVolumetricCloudManager()) {
            cloudManager->Update(cameraPosition, viewMatrix, projMatrix,
                                 atmosphereManager, Time::DeltaTime());
        }
    }

    void EnvironmentFeature::UpdateFog(SceneContext& ctx)
    {
        // フォグは空・大気を必要としないため、UpdateAtmosphere と違って
        // SkyBox の有無でガードしない（大気非対応シーンでもフォグは使える）。
        auto* domainContext = ctx.engine ? ctx.engine->GetRenderDomainContext() : nullptr;
        auto* fogManager = domainContext ? domainContext->GetFogManager() : nullptr;
        if (!fogManager) {
            return;
        }

        // 太陽内散乱に要るのは向きだけなので、サービスではなく値を渡す
        // （FogManager に LightManager を持たせない）。大気の太陽ライトを最優先し、
        // 無ければ最初の平行光を使う（大気非対応シーンでも内散乱が効く）
        Vector3 sunDirection{ 0.0f, -1.0f, 0.0f };
        bool hasSun = false;
        if (auto* lightManager = ctx.engine->GetService<LightManager>()) {
            const Light* sun = lightManager->GetAtmosphereSunLight();
            if (!sun) {
                sun = lightManager->GetDirectionalLight(0);
            }
            if (sun && sun->enabled) {
                sunDirection = sun->direction;
                hasSun = true;
            }
        }

        // このフレームはフォグを使うと宣言する（実際に合成するかは r.Fog.Enabled 次第）。
        // 深度復元用の行列とカメラ位置は FrameViews から FogPass が取るのでここでは渡さない。
        fogManager->Update(sunDirection, hasSun);
    }
}
