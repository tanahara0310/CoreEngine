#include "pch.h"
#include "StageLightsFeature.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Light/LightManager.h"
#include "Math/MathCore.h"
#include "Utility/CVar/CVar.h"

#ifdef USE_IMGUI
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "Utility/Debug/GameDebugUI.h"
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/ImGuiAll.h"
#endif

#include <algorithm>
#include <cmath>

namespace
{
    using namespace CoreEngine;

    constexpr float kRadToDeg = MathCore::Constants::kRadToDeg;

    /// パネルが扱う CVar の接頭辞
    constexpr const char* kCVarPrefix = "Game.StageLights";

#ifdef USE_IMGUI
    /// 設定パネルの編集対象（ドロワーは何もキャプチャせずこれを読むだけ）
    GameScene::StageLightsFeature* s_activeStageLights = nullptr;
#endif

    // ───────────────────────────────────────────────────────────────
    // 点灯カーブ（全ての灯りで共通）
    // ───────────────────────────────────────────────────────────────

    CVar<float> cvOnElevationDeg{
        "Game.StageLights.OnElevationDeg", 5.0f,
        "灯り始める太陽高度 [deg]。正の値にすると日没前から点き始める",
        CVarRange{ -20.0f, 30.0f } };

    CVar<float> cvFullElevationDeg{
        "Game.StageLights.FullElevationDeg", -6.0f,
        "全灯になる太陽高度 [deg]（-6 = 市民薄明の終わり）",
        CVarRange{ -30.0f, 20.0f } };

    // ───────────────────────────────────────────────────────────────
    // 追従灯（レールビルダー / トロッコ）
    // ───────────────────────────────────────────────────────────────

    CVar<bool> cvBuilderEnable{
        "Game.StageLights.Builder.Enable", true,
        "レールビルダー（赤い矢印）に灯りを付ける" };

    CVar<Vector3> cvBuilderOffset{
        "Game.StageLights.Builder.Offset", { 0.0f, 2.0f, 0.0f },
        "ビルダーの灯りの位置オフセット [m]（対象のワールド位置からの相対）" };

    CVar<Vector3> cvBuilderColor{
        "Game.StageLights.Builder.Color", { 1.0f, 0.86f, 0.62f },
        "ビルダーの灯りの色（リニア RGB。既定はランタン色）" };

    CVar<float> cvBuilderIntensity{
        "Game.StageLights.Builder.Intensity", 2500.0f,
        "ビルダーの灯りの光度 [cd]",
        CVarRange{ 0.0f, 100000.0f } };

    CVar<float> cvBuilderRange{
        "Game.StageLights.Builder.Range", 8.0f,
        "ビルダーの灯りの到達距離 [m]。マップは Z=0〜8 の 9m 幅しかないので、"
        "これを大きくすると光がマップの外の既定床へはみ出して、光源の無いオレンジの円が出る",
        CVarRange{ 0.5f, 100.0f } };

    CVar<bool> cvTrainEnable{
        "Game.StageLights.Train.Enable", true,
        "トロッコに灯りを付ける" };

    CVar<Vector3> cvTrainOffset{
        "Game.StageLights.Train.Offset", { 0.0f, 1.6f, 0.0f },
        "トロッコの灯りの位置オフセット [m]（対象のワールド位置からの相対）" };

    CVar<Vector3> cvTrainColor{
        "Game.StageLights.Train.Color", { 1.0f, 0.95f, 0.85f },
        "トロッコの灯りの色（リニア RGB。既定は前照灯の白）" };

    CVar<float> cvTrainIntensity{
        "Game.StageLights.Train.Intensity", 2000.0f,
        "トロッコの灯りの光度 [cd]",
        CVarRange{ 0.0f, 100000.0f } };

    CVar<float> cvTrainRange{
        "Game.StageLights.Train.Range", 3.5f,
        "トロッコの灯りの到達距離 [m]",
        CVarRange{ 0.5f, 100.0f } };

    /// レールビルダー・トロッコの GameObject 名（GameScene::OnInitialize の CreateObject と一致）
    constexpr const char* kBuilderObjectName = "RailBuilder";
    constexpr const char* kTrainObjectName = "Train";
}

namespace GameScene
{
    using namespace CoreEngine;

    // ==================== ライフサイクル ====================

    void StageLightsFeature::Initialize([[maybe_unused]] SceneContext& ctx)
    {
#ifdef USE_IMGUI
        EnsureSettingsPanelRegistered(ctx.engine);
        SetActiveForSettingsPanel(this);
#endif
    }

    void StageLightsFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        // GameObject の移動が確定した後に走る。FrameStart だと追従灯が
        // 走っているトロッコの 1 フレーム後ろへ置き去りになる
        if (phase != SceneUpdatePhase::PostObjectUpdate) {
            return;
        }

        auto* lightManager = ctx.engine ? ctx.engine->GetService<LightManager>() : nullptr;
        if (!lightManager) {
            return;
        }

        // 点灯量は太陽高度から直接求める
        sunElevationDeg_ = ComputeSunElevationDeg(*lightManager);
        litRatio_ = ComputeLitRatio(sunElevationDeg_);

        UpdateFollowLight(ctx, *lightManager, builderLight_, {
            kBuilderObjectName, "BuilderLight", cvBuilderEnable.Get(),
            cvBuilderOffset.Get(), cvBuilderColor.Get(),
            cvBuilderIntensity.Get(), cvBuilderRange.Get() });

        UpdateFollowLight(ctx, *lightManager, trainLight_, {
            kTrainObjectName, "TrainLight", cvTrainEnable.Get(),
            cvTrainOffset.Get(), cvTrainColor.Get(),
            cvTrainIntensity.Get(), cvTrainRange.Get() });
    }

    void StageLightsFeature::Finalize(SceneContext& ctx)
    {
#ifdef USE_IMGUI
        SetActiveForSettingsPanel(nullptr);
#endif
        if (auto* lightManager = ctx.engine ? ctx.engine->GetService<LightManager>() : nullptr) {
            lightManager->DestroyLight(builderLight_);
            lightManager->DestroyLight(trainLight_);
        }
        builderLight_ = {};
        trainLight_ = {};
    }

    // ==================== 追従灯 ====================

    const GameObject* StageLightsFeature::FindObjectByName(SceneContext& ctx, const char* name)
    {
        if (!ctx.gameObjectManager || !name) {
            return nullptr;
        }
        // シーンの GameObject は 20 体ほどなので毎フレーム走査で足りる。
        // ポインタをキャッシュすると、削除されたときにぶら下がる方が危ない
        for (const auto& object : ctx.gameObjectManager->GetAllObjects()) {
            if (object && object->IsActive() && !object->IsMarkedForDestroy()
                && object->GetName() == name) {
                return object.get();
            }
        }
        return nullptr;
    }

    void StageLightsFeature::UpdateFollowLight(SceneContext& ctx, LightManager& lightManager,
        LightHandle& handle, const FollowLightSettings& settings)
    {
        if (!settings.enable) {
            // 切られたら灯を返す（ポイントライトは 16 灯しか無いので抱えたままにしない）
            lightManager.DestroyLight(handle);
            handle = {};
            return;
        }

        const GameObject* target = FindObjectByName(ctx, settings.objectName);
        if (!target) {
            // 対象がまだ生成されていない・消えた: 消灯して待つ（灯は持ったまま）
            if (Light* light = lightManager.GetLight(handle)) {
                light->enabled = false;
            }
            return;
        }

        if (!lightManager.GetLight(handle)) {
            handle = lightManager.CreateLight(LightType::Point, settings.lightName);
            if (!handle.IsValid()) {
                return;  // ポイントライトの上限（16 灯）に当たった
            }
        }

        Light* light = lightManager.GetLight(handle);
        const Vector3 base = target->GetWorldPosition();

        light->position = {
            base.x + settings.offset.x,
            base.y + settings.offset.y,
            base.z + settings.offset.z,
        };
        light->color = settings.color;
        light->intensity = settings.intensity * litRatio_;
        light->range = settings.range;
        light->enabled = (litRatio_ > 0.001f);

        // パネルの確認用（どの灯りがどこに居るかを Development でも見られるように）
        (&handle == &builderLight_ ? builderLightPos_ : trainLightPos_) = light->position;
    }

    // ==================== 点灯量 ====================

    float StageLightsFeature::ComputeSunElevationDeg(LightManager& lightManager)
    {
        const Light* sun = lightManager.GetAtmosphereSunLight();
        if (!sun) {
            return 90.0f;  // 太陽が無いシーンは昼扱い（＝点灯しない）
        }
        // ライト方向は「太陽 → 地表」なので、太陽を見る方向の Y が sin(高度)
        const Vector3 direction = Normalize(sun->direction);
        return std::asin(std::clamp(-direction.y, -1.0f, 1.0f)) * kRadToDeg;
    }

    float StageLightsFeature::ComputeLitRatio(float sunElevationDeg)
    {
        const float on = cvOnElevationDeg.Get();
        const float full = cvFullElevationDeg.Get();

        // on（明るい側）から full（暗い側）へ落ちる間で 0 → 1
        float t = (on > full)
            ? std::clamp((on - sunElevationDeg) / (on - full), 0.0f, 1.0f)
            : ((sunElevationDeg <= on) ? 1.0f : 0.0f);

        // 点き始めと点き終わりの角を丸める（線形だと点灯の開始が唐突に見える）
        return t * t * (3.0f - 2.0f * t);
    }

    // ==================== 設定パネル ====================

#ifdef USE_IMGUI
    void StageLightsFeature::EnsureSettingsPanelRegistered(EngineSystem* engine)
    {
        static bool registered = false;
        if (registered || !engine) {
            return;
        }

        auto* debug = engine->GetDebugSubsystem();
        auto* gameDebugUI = debug ? debug->GetGameDebugUI() : nullptr;
        if (!gameDebugUI) {
            return;
        }

        gameDebugUI->RegisterAppEditor("Stage Lights", [] {
            if (s_activeStageLights) {
                s_activeStageLights->DrawSettingsImGui();
            } else {
                ImGui::TextDisabled("(このシーンには灯りがありません)");
            }
        });

        registered = true;
    }

    void StageLightsFeature::SetActiveForSettingsPanel(StageLightsFeature* stage)
    {
        s_activeStageLights = stage;
    }

    void StageLightsFeature::DrawSettingsImGui()
    {
        ImGui::Text("点灯 %.0f%%（太陽高度 %.1f°）", litRatio_ * 100.0f, sunElevationDeg_);
        ImGui::ProgressBar(litRatio_, ImVec2(-1, 0));

        const int litCount = (builderLight_.IsValid() ? 1 : 0) + (trainLight_.IsValid() ? 1 : 0);
        ImGui::Text("追従灯 %d / 16 灯", litCount);

        // Development ビルドにはライトのギズモが無い（_DEBUG 限定）ので、
        // 「画面のあの光は何なのか」をここで確かめられるようにしておく
        ImGui::TextDisabled("ビルダー %s (%.1f, %.1f, %.1f) r=%.1f",
            builderLight_.IsValid() ? "○" : "×",
            builderLightPos_.x, builderLightPos_.y, builderLightPos_.z, cvBuilderRange.Get());
        ImGui::TextDisabled("トロッコ   %s (%.1f, %.1f, %.1f) r=%.1f",
            trainLight_.IsValid() ? "○" : "×",
            trainLightPos_.x, trainLightPos_.y, trainLightPos_.z, cvTrainRange.Get());

        ImGui::Spacing();
        ImGui::TextDisabled("太陽高度 %.0f° で灯り始め、%.0f° で全灯",
            cvOnElevationDeg.Get(), cvFullElevationDeg.Get());
        ImGui::Spacing();

        CVarUI::DrawTree(kCVarPrefix);
    }
#endif
}
