#include "pch.h"
#include "PhysicsFeature.h"

#include "EngineSystem/EngineSystem.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/Time.h"

#ifdef CORE_EDITOR
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#endif

namespace CoreEngine
{
    namespace {
        // CVar はファイルスコープの static で定義する（レジストリへの登録が起動時に済む）

        constexpr const char* kPhysicsCVarPrefix = "sys.Physics";

        CVar<bool> cvEnabled{
            "sys.Physics.Enabled", true,
            "物理シミュレーションを進める" };

        CVar<Vector3> cvGravity{
            "sys.Physics.Gravity", Vector3{ 0.0f, -9.81f, 0.0f },
            "重力加速度（m/s^2）" };

        CVar<float> cvFixedStep{
            "sys.Physics.FixedStep", 1.0f / 60.0f,
            "固定ステップ 1 回の秒数", CVarRange{ 1.0f / 240.0f, 1.0f / 20.0f } };

        CVar<int> cvMaxSubSteps{
            "sys.Physics.MaxSubSteps", 4,
            "1 フレームで進めるステップ数の上限", CVarRange{ 1.0f, 16.0f } };

#ifdef CORE_EDITOR
        /// 設定パネルが編集する Feature（ドロワーは何もキャプチャせずこれを読む）
        PhysicsFeature* s_activePhysics = nullptr;
#endif
    }

    void PhysicsFeature::Initialize(SceneContext& ctx)
    {
        ApplyCVars();
        world_.Reset();

#ifdef CORE_EDITOR
        EnsureSettingsPanelRegistered(ctx.engine);
        SetActiveForSettingsPanel(this);
#else
        (void)ctx;
#endif
    }

    void PhysicsFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        (void)ctx;

        if (phase != SceneUpdatePhase::PostObjectUpdate) {
            return;
        }

        ApplyCVars();

        if (!cvEnabled.Get()) {
            return;
        }

        // timeScale を掛けた経過時間を積む（ヒットストップ・スローがそのまま効く）
        world_.Advance(Time::DeltaTime());
    }

    void PhysicsFeature::Finalize(SceneContext& ctx)
    {
        (void)ctx;

        world_.Reset();

#ifdef CORE_EDITOR
        if (s_activePhysics == this) {
            SetActiveForSettingsPanel(nullptr);
        }
#endif
    }

    void PhysicsFeature::ApplyCVars()
    {
        world_.SetGravity(cvGravity.Get());
        world_.SetFixedDeltaTime(cvFixedStep.Get());
        world_.SetMaxSubSteps(cvMaxSubSteps.Get());

        // スクリプトが読む Time の固定ステップ幅を物理と揃える
        Time::SetFixedDeltaTime(cvFixedStep.Get());
    }

#ifdef CORE_EDITOR

    void PhysicsFeature::EnsureSettingsPanelRegistered(EngineSystem* engine)
    {
        static bool registered = false;
        if (registered || !engine) {
            return;
        }

        Editor::EditorPanelRegistry::Get().Register({
            .id = "Physics",
            .placement = Editor::PanelPlacement::SettingsSection,
            .draw = [] {
                if (s_activePhysics) {
                    s_activePhysics->DrawSettingsImGui();
                } else {
                    ImGui::TextDisabled("(シーンがありません)");
                }
            },
            });

        registered = true;
    }

    void PhysicsFeature::SetActiveForSettingsPanel(PhysicsFeature* physics)
    {
        s_activePhysics = physics;
    }

    void PhysicsFeature::DrawSettingsImGui()
    {
        ImGui::Text("ステップ %d 回/フレーム（累計 %llu 回）",
            world_.GetLastStepCount(),
            static_cast<unsigned long long>(world_.GetTotalStepCount()));
        ImGui::Text("進めた時間 %.2f 秒（未消化 %.4f 秒）",
            world_.GetSimulatedTime(), world_.GetAccumulator());

        if (world_.GetDroppedTime() > 0.0f) {
            ImGui::TextDisabled("上限超過で捨てた時間 %.2f 秒", world_.GetDroppedTime());
        }

        ImGui::Spacing();

        CVarUI::DrawTree(kPhysicsCVarPrefix);

        ImGui::Spacing();
        if (ImGui::Button("パラメータを既定値にリセット")) {
            CVarUI::ResetTree(kPhysicsCVarPrefix);
        }
    }

#endif
}
