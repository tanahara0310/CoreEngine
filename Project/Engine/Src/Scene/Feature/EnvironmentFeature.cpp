#include "pch.h"
#include "EnvironmentFeature.h"
#include "EngineSystem/EngineSystem.h"
#include "Camera/Camera.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/GameObject.h"
#include "Graphics/Render/SkyBox/SkyBoxComponent.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Fog/FogManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    void EnvironmentFeature::PostSceneInitialize(SceneContext& ctx)
    {
        // シーンが SkyBox を生成していない場合のみ自動生成するため、オブジェクトが出そろった後に行う
        SetupDefaultSky(ctx);
    }

    void EnvironmentFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        switch (phase) {
        case SceneUpdatePhase::PostLogic:
            // 大気散乱の更新（全ロジック更新後の最新の太陽・カメラ情報を反映する）
            UpdateAtmosphere(ctx);
            // フォグは空・大気の有無に依存しないので、大気更新の成否と無関係に呼ぶ
            UpdateFog(ctx);
            break;
        default:
            break;
        }
    }

    void EnvironmentFeature::Finalize(SceneContext&)
    {
        // 空は GameObjectManager が所有しているためポインタのみクリア
        skyBox_ = nullptr;
    }

    void EnvironmentFeature::SetupDefaultSky(SceneContext& ctx)
    {
        // シーン側（OnInitialize）で作った空があればそれを使う
        if (auto* skyBox = ctx.gameObjectManager->FindFirstComponent<SkyBoxComponent>()) {
            skyBox_ = skyBox;
            Logger::GetInstance().Infof(LogCategory::System,
                "BaseScene: シーン生成の SkyBox を採用");
            return;
        }

        // 無ければ既定の背景として、大気散乱の空を持つオブジェクトを作る（シーンには保存しない）
        auto owned = std::make_unique<GameObject>();
        owned->SetName("SkyBox");
        GameObject* const object = ctx.gameObjectManager->AddObject(std::move(owned));
        if (!object) {
            return;
        }
        object->SetSerializeEnabled(false);
        skyBox_ = object->AddComponent<SkyBoxComponent>();
        Logger::GetInstance().Infof(LogCategory::System,
            "BaseScene: 既定背景として大気散乱モードの SkyBox を自動生成");
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
