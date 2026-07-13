#include "pch.h"
#include "EnvironmentFeature.h"
#include "EngineSystem/EngineSystem.h"
#include "Camera/ICamera.h"
#include "GameObject/GameObjectManager.h"
#include "GameObjects/SkyBox/SkyBoxObject.h"
#include "GameObject/Ground/InfiniteGroundObject.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    void EnvironmentFeature::PostSceneInitialize(SceneContext& ctx)
    {
        // シーンが SkyBox を生成していない場合のみ自動生成するため OnInitialize() の後に行う
        SetupDefaultSky(ctx);

        // 既定の無限地面（y=0 のグレータイル床）のセットアップ
        SetupDefaultGround(ctx);
    }

    void EnvironmentFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        switch (phase) {
        case SceneUpdatePhase::PreObjectUpdate:
            // 既定の無限地面をカメラ XZ に追従させる（GameObject 更新前に位置を確定する）
            UpdateGroundPlane(ctx);
            break;
        case SceneUpdatePhase::PostLogic:
            // 大気散乱の更新（全ロジック更新後の最新の太陽・カメラ情報を反映する）
            UpdateAtmosphere(ctx);
            break;
        default:
            break;
        }
    }

    void EnvironmentFeature::Finalize(SceneContext&)
    {
        // SkyBox / 無限地面は GameObjectManager が所有しているためポインタのみクリア
        skyBox_ = nullptr;
        groundPlane_ = nullptr;
    }

    void EnvironmentFeature::SetupDefaultSky(SceneContext& ctx)
    {
        // シーン側（OnInitialize）で生成済みの SkyBox があればそれを採用する
        for (const auto& obj : ctx.gameObjectManager->GetAllObjects()) {
            if (auto* sceneSkyBox = dynamic_cast<SkyBoxObject*>(obj.get())) {
                skyBox_ = sceneSkyBox;
                Logger::GetInstance().Infof(LogCategory::System,
                    "BaseScene: シーン生成の SkyBox を採用 (背景モード={})",
                    skyBox_->IsAtmosphereMode() ? "大気散乱" : "キューブマップ");
                return;
            }
        }

        // 未生成なら既定の背景として大気散乱モードの SkyBox を自動生成する
        skyBox_ = ctx.gameObjectManager->AddObject(std::make_unique<SkyBoxObject>());
        skyBox_->SetActive(true);
        Logger::GetInstance().Infof(LogCategory::System,
            "BaseScene: 既定背景として大気散乱モードの SkyBox を自動生成");
    }

    void EnvironmentFeature::SetupDefaultGround(SceneContext& ctx)
    {
        // シーン側（OnInitialize）で生成済みの無限地面があればそれを採用する
        for (const auto& obj : ctx.gameObjectManager->GetAllObjects()) {
            if (auto* sceneGround = dynamic_cast<InfiniteGroundObject*>(obj.get())) {
                groundPlane_ = sceneGround;
                Logger::GetInstance().Infof(LogCategory::System,
                    "BaseScene: シーン生成の無限地面を採用");
                return;
            }
        }

        // シーンがオプトアウトしている場合は生成しない（独自地形・水面・2D シーン等）
        if (!wantsDefaultGround_) {
            return;
        }

        // 未生成なら既定の床として無限地面を自動生成する
        groundPlane_ = ctx.gameObjectManager->AddObject(std::make_unique<InfiniteGroundObject>());
        groundPlane_->SetActive(true);
        Logger::GetInstance().Infof(LogCategory::System,
            "BaseScene: 既定の床として無限地面を自動生成");
    }

    void EnvironmentFeature::UpdateGroundPlane(SceneContext& ctx)
    {
        if (!groundPlane_) {
            return;
        }

        // ゲームビューカメラの XZ に追従させる（タイルはワールド固定）
        if (const ICamera* camera = ctx.gameViewCamera3D) {
            groundPlane_->FollowCamera(camera->GetPosition());
        }
    }

    void EnvironmentFeature::UpdateAtmosphere(SceneContext& ctx)
    {
        // キューブマップモード中は AtmosphereManager を非アクティブのままにし、
        // LUT 生成・Aerial Perspective 合成をスキップさせる
        if (!skyBox_ || !skyBox_->IsAtmosphereMode()) {
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
        if (const ICamera* camera = ctx.gameViewCamera3D) {
            cameraPosition = camera->GetPosition();
            viewMatrix = camera->GetViewMatrix();
            projMatrix = camera->GetProjectionMatrix();
        }
        atmosphereManager->Update(cameraPosition, viewMatrix, projMatrix,
                                  ctx.engine->GetComponent<LightManager>());

        // 大気散乱の直後に雲を更新する（大気モード時のみ、という既存ガードの内側なので追加ガード不要）。
        // 雲は太陽情報・カメラ高度を AtmosphereManager から取得するため、大気 Update の後に呼ぶ。
        if (auto* cloudManager = domainContext->GetVolumetricCloudManager()) {
            auto* frameRate = ctx.engine->GetComponent<FrameRateController>();
            const float deltaTime = frameRate ? frameRate->GetDeltaTime() : 0.016f;
            cloudManager->Update(cameraPosition, viewMatrix, projMatrix,
                                 atmosphereManager, deltaTime);
        }
    }
}
