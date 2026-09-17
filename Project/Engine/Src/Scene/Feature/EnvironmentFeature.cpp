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

namespace
{
    using namespace CoreEngine;

    /// 太陽の進行方向（太陽→地表）の既定。控えがこれと違うときだけシーンの太陽へ当てる
    constexpr Vector3 kDefaultSunDirection{ -0.45073172f, -0.65011942f, 0.61170721f };

    /// 太陽の空（大気散乱）輝度スケールの既定（0 で照度からの自動換算）
    constexpr float kDefaultSunAtmosphereIntensity = 0.0f;

    /// @brief 太陽・月ライトの控え（エンジンの寿命。シーンを作り直しても引き継ぐ）
    /// @details 値の実体は LightManager の Light（シーン寿命）にあり、毎フレーム実体から控える。
    struct AtmosphereLightsCarry
    {
        Vector3 sunDirection = kDefaultSunDirection;
        float sunAtmosphereIntensity = kDefaultSunAtmosphereIntensity;

        bool moonEnabled = false;                          ///< 月（第2大気ライト）の有効/無効
        Vector3 moonDirection{ 0.0f, -0.5f, 0.8660254f };  ///< 進行方向（高度角 30°・方位角 180°）
        Vector3 moonColor{ 0.55f, 0.65f, 0.85f };          ///< 月光色（知覚的な青白さの美術値）
        float moonSurfaceIntensity = 114.0f;               ///< サーフェス直接光 [lx]
        float moonAtmosphereIntensity = 0.02f;             ///< 空（大気散乱）輝度スケール
    };

    AtmosphereLightsCarry& Carry()
    {
        static AtmosphereLightsCarry carry;
        return carry;
    }

    /// @brief ゼロベクトル等の不正値を弾いて正規化する
    Vector3 SafeDirection(const Vector3& dir, const Vector3& fallback)
    {
        const float lengthSq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
        if (lengthSq < 1e-8f) {
            return fallback;
        }
        return CoreEngine::Normalize(dir);
    }
}

namespace CoreEngine
{
    void EnvironmentFeature::PostSceneInitialize(SceneContext& ctx)
    {
        // シーンが SkyBox を生成していない場合のみ自動生成するため OnInitialize() の後に行う
        SetupDefaultSky(ctx);

        // 保存済みの太陽・月設定を復元する。ライト（LightingFeature 生成）と
        // シーン OnInitialize の両方より後のこの時点で流し込むことで、
        // 復元値が最終的な起点になる
        RestoreAtmosphereLights(ctx);
    }

    void EnvironmentFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        switch (phase) {
        case SceneUpdatePhase::PostLogic:
            // 太陽・月ライトの現在値を控える（エディタ・ギズモ・
            // シーンコードのどこから変更されても拾えるよう、実体側から毎フレーム）
            CaptureAtmosphereLights(ctx);
            // 大気散乱の更新（全ロジック更新後の最新の太陽・カメラ情報を反映する）
            UpdateAtmosphere(ctx);
            // フォグは空・大気の有無に依存しないので、大気更新の成否と無関係に呼ぶ
            UpdateFog(ctx);
            break;
        default:
            break;
        }
    }

    void EnvironmentFeature::Finalize(SceneContext& ctx)
    {
        // 最後の状態を控えておく（最終フレームの Update 以降の変更を取りこぼさない）。
        // ライトのクリア（SceneManager::DoChangeScene の ClearAllLights）より前に行う
        CaptureAtmosphereLights(ctx);

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

    void EnvironmentFeature::RestoreAtmosphereLights(SceneContext& ctx)
    {
        auto* lightManager = ctx.engine ? ctx.engine->GetService<LightManager>() : nullptr;
        if (!lightManager) {
            return;
        }

        // ===== 太陽（LightingFeature の既定ライトへのフォールバック込みで取得） =====
        // 太陽ライトはシーン側が独自の向き・強度を設定していることがあるため、
        // 「コード既定から変更されている項目」だけを上書きする。全項目を無条件に
        // 流し込むと、保存していない項目のコード既定値でシーンの設定を潰してしまう
        const AtmosphereLightsCarry& carry = Carry();
        if (Light* sun = lightManager->GetAtmosphereSunLight()) {
            if (carry.sunDirection != kDefaultSunDirection) {
                sun->direction = SafeDirection(carry.sunDirection, sun->direction);
            }
            if (carry.sunAtmosphereIntensity != kDefaultSunAtmosphereIntensity) {
                sun->atmosphereIntensity = carry.sunAtmosphereIntensity;
            }
            // 変化は AtmosphereManager::Update() が自動検知して Sky-View LUT を再生成する
        }

        // ===== 月（オプトイン。有効で保存されていればライトを生成して復元） =====
        Light* moon = lightManager->GetAtmosphereMoonLight();
        if (!moon && carry.moonEnabled) {
            // AtmosphereEditor::ApplyMoonSettings と同じ手順で第2ディレクショナルライトを生成する
            LightHandle moonHandle = lightManager->CreateLight(LightType::Directional, "Moon");
            moon = lightManager->GetLight(moonHandle);
            if (moon) {
                moon->isAtmosphereMoon = true;
            }
        }
        if (moon) {
            // 月は大気の月として作られた時点でシーン固有の初期状態を持たないため、
            // 太陽と違い全項目を無条件に流し込む
            moon->enabled = carry.moonEnabled;
            moon->direction = SafeDirection(carry.moonDirection, moon->direction);
            moon->color = carry.moonColor;
            moon->intensity = carry.moonSurfaceIntensity;
            moon->atmosphereIntensity = carry.moonAtmosphereIntensity;
        }
    }

    void EnvironmentFeature::CaptureAtmosphereLights(SceneContext& ctx)
    {
        auto* lightManager = ctx.engine ? ctx.engine->GetService<LightManager>() : nullptr;
        if (!lightManager) {
            return;
        }

        // 太陽のサーフェス照度・色は Lighting エディタ側の責務なので控えない
        AtmosphereLightsCarry& carry = Carry();
        if (const Light* sun = lightManager->GetAtmosphereSunLight()) {
            carry.sunDirection = sun->direction;
            carry.sunAtmosphereIntensity = sun->atmosphereIntensity;
        }

        if (const Light* moon = lightManager->GetAtmosphereMoonLight()) {
            carry.moonEnabled = moon->enabled;
            carry.moonDirection = moon->direction;
            carry.moonColor = moon->color;
            carry.moonSurfaceIntensity = moon->intensity;
            carry.moonAtmosphereIntensity = moon->atmosphereIntensity;
        } else {
            // 月ライトが無いシーンでは無効として控える（色などは次回有効化用に維持）
            carry.moonEnabled = false;
        }
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
