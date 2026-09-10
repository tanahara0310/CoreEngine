#include "pch.h"
#include "EngineSystem/EngineSystem.h"
#include "Input/InputManager.h"
#include "Graphics/Model/ModelManager.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "Graphics/Primitive/SphereMeshGenerator.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"

#ifdef _DEBUG
#include "Editor/Camera/CameraDebugUI.h"
#endif

#include "TestScene.h"
#include "Camera/Shake/CameraShake.h"
#include "Camera/Shake/CameraShakeEvent.h"
#include "Camera/Shake/CameraShakePresets.h"
#include "Input/InputQuery.h"
#include "Utility/Event/EventBus.h"
#include "WinApp/WinApp.h"
#include "Scene/SceneManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Editor/Environment/AtmosphereEditor.h"
#include "Utility/FrameRate/Time.h"

#include <cstdlib>
#include <format>
#include <iostream>

using namespace CoreEngine::MathCore;

namespace
{
    // Assets に音を置いていないので Windows 標準の WAV を直接指す。
    // AudioPathResolver はドライブレター付きのパスをそのまま通すので、
    // Assets 配下と同じ書き味で絶対パスも渡せる
    constexpr const char* kSeSoundPath = "C:/Windows/Media/Windows Notify System Generic.wav";
    constexpr const char* kBgmSoundPath = "C:/Windows/Media/Ring01.wav";
}

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
        auto modelManager = engine_->GetService<ModelManager>();

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
        // LightingFeature の既定値（天頂・シェーダー単位 intensity=1 相当）は大気散乱が
        // 期待する輝度スケールと整合しないため明示的に上書きする（他の大気シーンと同じ定石）。
        if (Light* sun = GetDirectionalLight()) {
            sun->direction = AtmosphereEditor::ComputeSunLightDirection(35.0f, 25.0f);
            // 空（大気・雲）の輝度スケールと、サーフェスの直接光は単位系が別なので分離して与える
            sun->atmosphereIntensity = 20.0f;
            sun->intensity = kAtmosphereSunIlluminanceLux;
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

                // コンポーネント合成で組む（専用クラスは不要）
                // sphere.obj というモデル資産は存在しないので、エンジンの
                // プリミティブ生成（UV 球）で作る
                auto* sphere = CreateObject("Sphere");
                sphere->AddComponent<MeshRendererComponent>(
                    std::make_unique<SphereMeshGenerator>(1.0f));

                auto& transform = sphere->GetComponent<TransformComponent>()->Get();
                transform.translate = {
                    originX + col * kSpacing,
                    originY + row * kSpacing,
                    0.0f
                };
                transform.scale = { 1.0f, 1.0f, 1.0f };

                auto* material = sphere->AddComponent<MaterialComponent>();
                material->SetPBR(metallic, roughness, 1.0f);
                material->SetIBLIntensity(1.0f);

                sphere->SetActive(true);
            }
        }

        // グリッドは床の上（y=1.5〜16.5）に持ち上がったため、その中心を正面に捉える
        const float gridCenterY = kGridBaseY + (kMetallicSteps - 1) * kSpacing * 0.5f;
        SetReleaseCameraTransform({ 0.0f, gridCenterY, -30.0f });

    }

    void TestScene::OnUpdate()
    {
        auto inputManager = engine_->GetService<InputManager>();
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

        UpdateCameraShakeTest(input);
    }

    void TestScene::UpdateCameraShakeTest(const InputQuery& input)
    {
        // 揺れが見えるのはゲーム視点のときだけ。エディタ視点（キー 1）では
        // ゲームカメラを揺らしていても画面は動かない
        if (input.IsKeyTriggered(DIK_4)) {
            CameraShake::Play(CameraShakePresets::Hit());
            logger.Logf(LogLevel::Info, LogCategory::Game, "CameraShake: Hit");
        }

        if (input.IsKeyTriggered(DIK_5)) {
            CameraShake::Play(CameraShakePresets::HeavyHit());
            logger.Logf(LogLevel::Info, LogCategory::Game, "CameraShake: HeavyHit");
        }

        if (input.IsKeyTriggered(DIK_6)) {
            // 球体グリッドの中心を爆心にする。カメラは -Z 側に居るので押し戻される向きになる
            const Vector3 explosionPosition = { 0.0f, 9.0f, 0.0f };
            CameraShake::Play(CameraShakePresets::Explosion(), explosionPosition);
            CameraShake::Play(CameraShakePresets::Hit());   // 高周波のガタつきを重ねる
            logger.Logf(LogLevel::Info, LogCategory::Game, "CameraShake: Explosion");
        }

        if (input.IsKeyTriggered(DIK_7)) {
            CameraShake::Play(CameraShakePresets::Recoil());
            logger.Logf(LogLevel::Info, LogCategory::Game, "CameraShake: Recoil");
        }

        if (input.IsKeyTriggered(DIK_8)) {
            CameraShake::Play(CameraShakePresets::Landing());
            logger.Logf(LogLevel::Info, LogCategory::Game, "CameraShake: Landing");
        }

        // 無限に続く揺れ。もう一度押すとフェードアウトして止まる
        if (input.IsKeyTriggered(DIK_9)) {
            if (earthquakeHandle_ != 0) {
                CameraShake::Stop(earthquakeHandle_, 1.0f);
                earthquakeHandle_ = 0;
                logger.Logf(LogLevel::Info, LogCategory::Game, "CameraShake: Earthquake stop");
            } else {
                earthquakeHandle_ = CameraShake::Play(CameraShakePresets::Earthquake());
                logger.Logf(LogLevel::Info, LogCategory::Game, "CameraShake: Earthquake start");
            }
        }

        // 蓄積型。連打すると揺れが強くなり、離すと自然に収まる
        if (input.IsKeyPressed(DIK_T)) {
            CameraShake::AddTrauma(0.06f);
        }

        // EventBus 経由の入口も同じ揺れになることの確認
        if (input.IsKeyTriggered(DIK_Y)) {
            EventBus::GetInstance().Publish(
                CameraShakeEvent::Make(CameraShakePresets::HeavyHit()));
            logger.Logf(LogLevel::Info, LogCategory::Game, "CameraShake: HeavyHit (EventBus)");
        }

        // すべて停止
        if (input.IsKeyTriggered(DIK_0)) {
            CameraShake::StopAll(0.3f);
            earthquakeHandle_ = 0;
            logger.Logf(LogLevel::Info, LogCategory::Game, "CameraShake: StopAll");
        }
    }

}


