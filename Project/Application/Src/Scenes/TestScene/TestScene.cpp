#include "pch.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Model/ModelManager.h"

#ifdef _DEBUG
#include "Editor/Camera/CameraDebugUI.h"
#endif

#include "TestScene.h"
#include "WinApp/WinApp.h"
#include "Scene/SceneManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Editor/Environment/AtmosphereEditor.h"

#include <iostream>

using namespace CoreEngine::MathCore;

// アプリケーションの初期化

namespace CoreEngine
{
    void TestScene::OnInitialize()
    {
        SetSceneName("TestScene");

        ///========================================================
        // モデルの読み込みと初期化
        ///========================================================

        // コンポーネントを直接取得
        auto modelManager = engine_->GetComponent<ModelManager>();

        if (!modelManager) {
            return; // 必須コンポーネントがない場合は終了
        }

        // ===== モデルリソースを並列プリロード =====
        // 全モデルを事前にバックグラウンドスレッドで並列読み込みし、
        // 後続の CreateObject 時にはキャッシュヒットで即座に返る
        //modelManager->PreloadModels({ "sphere.obj" });

        // ===== 太陽ライト =====
        // 空は BaseScene が既定で大気散乱モードの SkyBox（＋雲）を自動生成するため、
        // 以前の静的 HDR キューブマップ＋IBL セットアップは廃止した。
        // 球体の環境反射は大気の空キューブマップ（スペキュラIBL）が担う。
        // BaseScene::SetupLight() の既定値（天頂・intensity=1）は大気散乱が期待する
        // 輝度スケールと整合しないため明示的に上書きする（他の大気シーンと同じ定石）。
        if (directionalLight_) {
            directionalLight_->direction = AtmosphereEditor::ComputeSunLightDirection(35.0f, 25.0f);
            // 空（大気・雲）の輝度スケールと、サーフェスの直接光は単位系が別なので分離して与える
            directionalLight_->atmosphereIntensity = 20.0f;
            directionalLight_->intensity = kAtmosphereSunIlluminanceLux;
        }

        // ===== PBR パラメータテスト用球体グリッド =====
        // 列（X 軸）: Roughness  0.0（左=鏡面） → 1.0（右=粗面）
        // 行（Y 軸）: Metallic   0.0（下=非金属） → 1.0（上=金属）
        //
        //  Metallic
        //  1.0 ┌──────────────────────────────────┐
        //      │  金属 × 各 Roughness              │
        //  0.5 │  半金属 × 各 Roughness            │
        //  0.0 └──────────────────────────────────┘
        //      0.0   0.17  0.33  0.5  0.67  0.83  1.0  Roughness

        constexpr int   kRoughnessSteps = 7;   // 列数（Roughness 軸）
        constexpr int   kMetallicSteps = 7;   // 行数（Metallic 軸）
        constexpr float kSpacing = 2.5f; // 球体間の間隔

        // グリッド原点。X は中央が座標原点になるよう計算する。
        // Y は既定の無限遠タイル床（y=0）より上にグリッド全体が載るよう下端を持ち上げる。
        constexpr float kGridBaseY = 1.5f;   // 最下段の中心高さ（球半径 1 を考慮して床に埋まらない）
        const float originX = -(kRoughnessSteps - 1) * kSpacing * 0.5f;
        const float originY = kGridBaseY;

        for (int row = 0; row < kMetallicSteps; ++row)
        {
            const float metallic = static_cast<float>(row) / static_cast<float>(kMetallicSteps - 1);

            for (int col = 0; col < kRoughnessSteps; ++col)
            {
                const float roughness = static_cast<float>(col) / static_cast<float>(kRoughnessSteps - 1);

                auto sphere = CreateObject<ModelObject>("sphere.obj");
                sphere->GetTransform().translate = {
                    originX + col * kSpacing,
                    originY + row * kSpacing,
                    0.0f
                };
                sphere->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
                sphere->SetPBRParameters(metallic, roughness, 1.0f);
                sphere->SetIBLEnabled(true);
                sphere->SetIBLIntensity(1.0f);
                sphere->SetActive(true);
            }
        }

        // グリッドは床の上（y=1.5〜16.5）に持ち上がったため、その中心を正面に捉える
        const float gridCenterY = kGridBaseY + (kMetallicSteps - 1) * kSpacing * 0.5f;
        SetReleaseCameraTransform({ 0.0f, gridCenterY, -30.0f });
    }

    void TestScene::OnUpdate()
    {
        auto inputManager = engine_->GetComponent<InputManager>();
        if (!inputManager) {
            return;
        }

        // Tabキーでテストシーンをリスタート
        auto& input = inputManager->GetQuery();
        if (input.IsKeyTriggered(DIK_TAB)) {
            if (sceneManager_) {
                sceneManager_->ChangeScene("TestScene");
            }
            return;
        }
    }

    void TestScene::Draw()
    {
        BaseScene::Draw();
    }
}


