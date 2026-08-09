#include "pch.h"
#include "EnvironmentFeature.h"
#include "EngineSystem/EngineSystem.h"
#include "Camera/Camera.h"
#include "GameObject/GameObjectManager.h"
#include "GameObjects/SkyBox/SkyBoxObject.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Utility/Logger/Logger.h"

namespace
{
    using namespace CoreEngine;

    /// 太陽・月ライトの保存用 CVar。
    /// 値の実体は LightManager が持つ Light（シーン寿命）側にあるため、
    /// これらはエンジン寿命の「鏡」として振る舞う（毎フレーム実体から写す）。
    /// 編集 UI は Environment ツリーの Atmosphere エディタ（高度角・方位角・
    /// スカイマップのドラッグ）が担当するので、自動生成 UI には出さない。
    constexpr CVarFlags kMirrorFlags = CVarFlags::NoUI;

    CVar<Vector3> cvSunDirection{
        "r.AtmosphereLights.SunDirection", { 0.0f, -1.0f, 0.0f },
        "大気の太陽ライトの進行方向（太陽→地表）", {}, kMirrorFlags };
    CVar<float> cvSunAtmosphereIntensity{
        "r.AtmosphereLights.SunAtmosphereIntensity", 0.0f,
        "太陽の空（大気散乱）輝度スケール。0 で照度からの自動換算",
        CVarRange{ 0.0f, 100.0f }, kMirrorFlags };

    CVar<bool> cvMoonEnabled{
        "r.AtmosphereLights.MoonEnabled", false,
        "月（第2大気ライト）の有効/無効", {}, kMirrorFlags };
    CVar<Vector3> cvMoonDirection{
        // 既定は高度角 30°・方位角 180°（太陽の反対側）を向けた進行方向
        "r.AtmosphereLights.MoonDirection", { 0.0f, -0.5f, 0.8660254f },
        "月ライトの進行方向（月→地表）", {}, kMirrorFlags };
    CVar<Vector3> cvMoonColor{
        "r.AtmosphereLights.MoonColor", { 0.55f, 0.65f, 0.85f },
        "月光色（知覚的な青白さの美術値）", {}, kMirrorFlags };
    CVar<float> cvMoonSurfaceIntensity{
        "r.AtmosphereLights.MoonSurfaceIntensity", 114.0f,
        "月光のサーフェス直接光 [lx]", CVarRange{ 0.0f, 1000.0f }, kMirrorFlags };
    CVar<float> cvMoonAtmosphereIntensity{
        "r.AtmosphereLights.MoonAtmosphereIntensity", 0.02f,
        "月光の空（大気散乱）輝度スケール", CVarRange{ 0.0f, 1.0f }, kMirrorFlags };

    /// @brief ゼロベクトル等の不正値を弾いて正規化する
    Vector3 SafeDirection(const Vector3& dir, const Vector3& fallback)
    {
        const float lengthSq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
        if (lengthSq < 1e-8f) {
            return fallback;
        }
        return MathCore::Vector::Normalize(dir);
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
        RestoreAtmosphereLightsFromCVars(ctx);
    }

    void EnvironmentFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        switch (phase) {
        case SceneUpdatePhase::PostLogic:
            // 太陽・月ライトの現在値を保存用 CVar へ写す（エディタ・ギズモ・
            // シーンコードのどこから変更されても拾えるよう、実体側から毎フレーム）
            MirrorAtmosphereLightsToCVars(ctx);
            // 大気散乱の更新（全ロジック更新後の最新の太陽・カメラ情報を反映する）
            UpdateAtmosphere(ctx);
            break;
        default:
            break;
        }
    }

    void EnvironmentFeature::Finalize(SceneContext& ctx)
    {
        // 最後の状態を CVar へ写しておく（最終フレームの Update 以降の変更を取りこぼさない）。
        // ライトのクリア（SceneManager::DoChangeScene の ClearAllLights）より前に行う
        MirrorAtmosphereLightsToCVars(ctx);

        // SkyBox は GameObjectManager が所有しているためポインタのみクリア
        skyBox_ = nullptr;
    }

    void EnvironmentFeature::SetupDefaultSky(SceneContext& ctx)
    {
        // シーン側（OnInitialize）で生成済みの SkyBox があればそれを採用する
        // （具象型のダウンキャストではなく SceneTag で探す）
        if (auto* tag = ctx.gameObjectManager->FindFirstComponent<SceneTagComponent<SkyBoxObject>>()) {
            skyBox_ = tag->Get();
            Logger::GetInstance().Infof(LogCategory::System,
                "BaseScene: シーン生成の SkyBox を採用");
            return;
        }

        // 未生成なら既定の背景として大気散乱モードの SkyBox を自動生成する
        skyBox_ = ctx.gameObjectManager->AddObject(std::make_unique<SkyBoxObject>());
        skyBox_->SetActive(true);
        Logger::GetInstance().Infof(LogCategory::System,
            "BaseScene: 既定背景として大気散乱モードの SkyBox を自動生成");
    }

    void EnvironmentFeature::RestoreAtmosphereLightsFromCVars(SceneContext& ctx)
    {
        auto* lightManager = ctx.engine ? ctx.engine->GetService<LightManager>() : nullptr;
        if (!lightManager) {
            return;
        }

        // ===== 太陽（LightingFeature の既定ライトへのフォールバック込みで取得） =====
        // 太陽ライトはシーン側が独自の向き・強度を設定していることがあるため、
        // 「コード既定から変更されている項目」だけを上書きする。全項目を無条件に
        // 流し込むと、保存していない項目のコード既定値でシーンの設定を潰してしまう
        if (Light* sun = lightManager->GetAtmosphereSunLight()) {
            if (cvSunDirection.IsModified()) {
                sun->direction = SafeDirection(cvSunDirection.Get(), sun->direction);
            }
            if (cvSunAtmosphereIntensity.IsModified()) {
                sun->atmosphereIntensity = cvSunAtmosphereIntensity.Get();
            }
            // 変化は AtmosphereManager::Update() が自動検知して Sky-View LUT を再生成する
        }

        // ===== 月（オプトイン。有効で保存されていればライトを生成して復元） =====
        Light* moon = lightManager->GetAtmosphereMoonLight();
        if (!moon && cvMoonEnabled.Get()) {
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
            moon->enabled = cvMoonEnabled.Get();
            moon->direction = SafeDirection(cvMoonDirection.Get(), moon->direction);
            moon->color = cvMoonColor.Get();
            moon->intensity = cvMoonSurfaceIntensity.Get();
            moon->atmosphereIntensity = cvMoonAtmosphereIntensity.Get();
        }
    }

    void EnvironmentFeature::MirrorAtmosphereLightsToCVars(SceneContext& ctx)
    {
        auto* lightManager = ctx.engine ? ctx.engine->GetService<LightManager>() : nullptr;
        if (!lightManager) {
            return;
        }

        // CVar::Set は値が実際に変わったときだけ変更通番を進めるため、毎フレーム呼んでよい。
        // 太陽のサーフェス照度・色は Lighting エディタ側の責務なので写さない
        if (const Light* sun = lightManager->GetAtmosphereSunLight()) {
            cvSunDirection.Set(sun->direction);
            cvSunAtmosphereIntensity.Set(sun->atmosphereIntensity);
        }

        if (const Light* moon = lightManager->GetAtmosphereMoonLight()) {
            cvMoonEnabled.Set(moon->enabled);
            cvMoonDirection.Set(moon->direction);
            cvMoonColor.Set(moon->color);
            cvMoonSurfaceIntensity.Set(moon->intensity);
            cvMoonAtmosphereIntensity.Set(moon->atmosphereIntensity);
        } else {
            // 月ライトが無いシーンでは無効として記録する（色などは次回有効化用に維持）
            cvMoonEnabled.Set(false);
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
            auto* frameRate = ctx.engine->GetService<FrameRateController>();
            const float deltaTime = frameRate ? frameRate->GetDeltaTime() : 0.016f;
            cloudManager->Update(cameraPosition, viewMatrix, projMatrix,
                                 atmosphereManager, deltaTime);
        }
    }
}
