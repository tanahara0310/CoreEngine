#include "AssignmentScene.h"
#include <EngineSystem.h>

#ifdef _DEBUG
#include "Engine/Camera/Debug/CameraDebugUI.h"
#endif

#include "WinApp/WinApp.h"
#include "Scene/SceneManager.h"
#include "Engine/Graphics/Render/RenderManager.h"
#include "Engine/Graphics/Render/Model/ModelRenderer.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Graphics/Light/LightManager.h"

using namespace CoreEngine::MathCore;

namespace CoreEngine
{
    void AssignmentScene::Initialize(EngineSystem* engine)
    {
        BaseScene::Initialize(engine);

        ///========================================================
        // モデルの読み込みと初期化
        ///========================================================

        // コンポーネントを直接取得
        auto dxCommon = engine_->GetComponent<DirectXCommon>();
        auto modelManager = engine_->GetComponent<ModelManager>();
        auto renderManager = engine_->GetComponent<RenderManager>();
        auto lightManager = engine_->GetComponent<LightManager>();

        if (!dxCommon || !modelManager || !renderManager || !lightManager) {
            return; // 必須コンポーネントがない場合は終了
        }

        // ===== 3Dゲームオブジェクトの生成と初期化 =====

        // Sphereオブジェクト
        sphere_ = CreateObject<SphereObject>();
        sphere_->Initialize();
        sphere_->SetActive(true);
        sphere_->GetTransform().translate = { 0.0f, 2.0f, 0.0f };
        sphere_->GetTransform().scale = { 2.0f, 2.0f, 2.0f };

        // Planeオブジェクト
        plane_ = CreateObject<Plane>();
        plane_->Initialize();
        plane_->SetActive(true);
        plane_->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
        plane_->GetTransform().scale = { 10.0f, 10.0f, 10.0f };


        // WalkModelオブジェクト1（モンスターボールの左側）
        walkModel1_ = CreateObject<WalkModelObject>();
        walkModel1_->Initialize();
        walkModel1_->SetActive(true);
        walkModel1_->GetTransform().translate = { -4.0f, 0.0f, 0.0f };
        walkModel1_->GetTransform().scale = { 2.0f, 2.0f, 2.0f };

        // WalkModelオブジェクト2（モンスターボールの右側）
        walkModel2_ = CreateObject<WalkModelObject>();
        walkModel2_->Initialize();
        walkModel2_->SetActive(true);
        walkModel2_->GetTransform().translate = { 4.0f, 0.0f, 0.0f };
        walkModel2_->GetTransform().scale = { 2.0f, 2.0f, 2.0f };

        // ===== ライトの設定 =====

        // ベースシーンのディレクショナルライト（インデックス0）の輝度を変更
        auto directionalLight = lightManager->GetDirectionalLight(0);
        if (directionalLight) {
            directionalLight->intensity = 1.0f;
        }

        // ===== ポイントライト x2（左右に配置、赤と青）=====

        // ポイントライト1 - 左側（赤色）
        auto pointLight1 = lightManager->AddPointLight();
        if (pointLight1) {
            pointLight1->color = { 1.0f, 0.2f, 0.2f, 1.0f };
            pointLight1->position = { -19.0f, 2.5f, 0.0f };
            pointLight1->intensity = 3.0f;
            pointLight1->radius = 10.0f;
            pointLight1->decay = 1.0f;
            pointLight1->enabled = false;
        }

        // ポイントライト2 - 右側（青色）
        auto pointLight2 = lightManager->AddPointLight();
        if (pointLight2) {
            pointLight2->color = { 0.2f, 0.2f, 1.0f, 1.0f };
            pointLight2->position = { 19.0f, 2.5f, 0.0f };
            pointLight2->intensity = 3.0f;
            pointLight2->radius = 10.0f;
            pointLight2->decay = 1.0f;
            pointLight2->enabled = false;
        }

        // ===== スポットライト x2（前後に配置、黄色と緑色）=====

        // スポットライト1 - 前側（黄色）
        auto spotLight1 = lightManager->AddSpotLight();
        if (spotLight1) {
            spotLight1->color = { 1.0f, 1.0f, 0.2f, 1.0f };
            spotLight1->position = { 0.0f, 8.0f, -24.0f };
            spotLight1->direction = Vector::Normalize({ 0.0f, -0.7f, 1.0f });
            spotLight1->intensity = 4.0f;
            spotLight1->distance = 20.0f;
            spotLight1->decay = 1.0f;
            spotLight1->cosAngle = std::cos(30.0f * 3.14159f / 180.0f);  // 30度
            spotLight1->cosFalloffStart = std::cos(20.0f * 3.14159f / 180.0f);  // 20度
            spotLight1->enabled = false;
        }

        // スポットライト2 - 後側（緑色）
        auto spotLight2 = lightManager->AddSpotLight();
        if (spotLight2) {
            spotLight2->color = { 0.2f, 1.0f, 0.2f, 12.0f };
            spotLight2->position = { 0.0f, 8.0f, 24.0f };
            spotLight2->direction = Vector::Normalize({ 0.0f, -0.7f, -1.0f });
            spotLight2->intensity = 4.0f;
            spotLight2->distance = 20.0f;
            spotLight2->decay = 1.0f;
            spotLight2->cosAngle = std::cos(30.0f * 3.14159f / 180.0f);  // 30度
            spotLight2->cosFalloffStart = std::cos(20.0f * 3.14159f / 180.0f);  // 20度
            spotLight2->enabled = false;
        }

        // ===== エリアライト x2（左右上に配置、暖色と寒色）=====

        // エリアライト1 - 左上（暖色系）
        auto areaLight1 = lightManager->AddAreaLight();
        if (areaLight1) {
            areaLight1->color = { 1.0f, 0.8f, 0.6f, 1.0f };  // オレンジ系暖色
            areaLight1->position = { -6.0f, 10.0f, 0.0f };
            areaLight1->normal = { 0.0f, -1.0f, 0.0f };  // 下向き
            areaLight1->intensity = 5.0f;
            areaLight1->width = 6.0f;
            areaLight1->height = 6.0f;
            areaLight1->right = { 1.0f, 0.0f, 0.0f };
            areaLight1->up = { 0.0f, 0.0f, 1.0f };
            areaLight1->range = 18.0f;
            areaLight1->enabled = false;
        }

        // エリアライト2 - 右上（寒色系）
        auto areaLight2 = lightManager->AddAreaLight();
        if (areaLight2) {
            areaLight2->color = { 0.6f, 0.8f, 1.0f, 1.0f };  // 青系寒色
            areaLight2->position = { 6.0f, 10.0f, 0.0f };
            areaLight2->normal = { 0.0f, -1.0f, 0.0f };  // 下向き
            areaLight2->intensity = 5.0f;
            areaLight2->width = 6.0f;
            areaLight2->height = 6.0f;
            areaLight2->right = { 1.0f, 0.0f, 0.0f };
            areaLight2->up = { 0.0f, 0.0f, 1.0f };
            areaLight2->range = 18.0f;
            areaLight2->enabled = false;
        }

#ifdef _DEBUG
        auto console = engine_->GetConsole();
        if (console) {
            console->LogInfo("AssignmentScene: シーン初期化完了");
            console->LogInfo("AssignmentScene: ライト配置完了");
            console->LogInfo("  - ディレクショナルライト x1（環境光・輝度0.1）");
            console->LogInfo("  - ポイントライト x2（左右配置・赤青）");
            console->LogInfo("  - スポットライト x2（前後配置・黄緑）");
            console->LogInfo("  - エリアライト x2（左右上配置・暖色寒色）");
        }
#endif
    }

    void AssignmentScene::OnUpdate()
    {
        // 特にシーン固有の更新処理は不要
        // ライトの制御はBaseSceneの「ライト制御」タブで行う
    }

    void AssignmentScene::Draw()
    {
        BaseScene::Draw();
    }

    void AssignmentScene::Finalize()
    {
#ifdef _DEBUG
        auto console = engine_->GetConsole();
        if (console) {
            console->LogInfo("AssignmentScene: シーン終了処理");
        }
#endif

        BaseScene::Finalize();
    }
}
