#include "pch.h"
#include "PhysicsFeature.h"

#include "CollisionFeature.h"
#include "Collision/ColliderComponent.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Physics/RigidbodyComponent.h"
#include "Script/ScriptComponent.h"
#include "Scene/Scene.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Graphics/Render/RenderManager.h"
#include "Scene/SceneManager.h"
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

        CVar<float> cvCorrectionRate{
            "sys.Physics.CorrectionRate", 0.2f,
            "めり込みを 1 ステップで押し戻す割合", CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvPenetrationSlop{
            "sys.Physics.PenetrationSlop", 0.01f,
            "押し戻さずに許すめり込みの深さ（m）", CVarRange{ 0.0f, 0.1f } };

        CVar<int> cvSolverIterations{
            "sys.Physics.SolverIterations", 8,
            "接触を解く繰り返し回数", CVarRange{ 1.0f, 32.0f } };

        CVar<bool> cvSleepEnabled{
            "sys.Physics.SleepEnabled", true,
            "止まった剛体を計算から外す" };

        CVar<float> cvSleepLinear{
            "sys.Physics.SleepLinearThreshold", 0.05f,
            "眠ってよい速さ（m/s）", CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvSleepAngular{
            "sys.Physics.SleepAngularThreshold", 0.12f,
            "眠ってよい角速度（rad/s）", CVarRange{ 0.0f, 2.0f } };

        CVar<float> cvTimeToSleep{
            "sys.Physics.TimeToSleep", 0.5f,
            "この秒数だけ止まり続けたら眠る", CVarRange{ 0.05f, 5.0f } };

        CVar<float> cvRestitutionThreshold{
            "sys.Physics.RestitutionThreshold", 1.0f,
            "この速さ未満の接近では跳ね返らせない（m/s）", CVarRange{ 0.0f, 5.0f } };

#ifdef CORE_EDITOR
        /// 設定パネルが編集する Feature（ドロワーは何もキャプチャせずこれを読む）
        PhysicsFeature* s_activePhysics = nullptr;
#endif
    }

    namespace {
        /// @brief Line パスのパイプラインを取得する（無ければ nullptr）
        LineRendererPipeline* GetLinePipeline(SceneContext& ctx)
        {
            auto* const renderManager = ctx.engine ? ctx.engine->GetService<RenderManager>() : nullptr;
            if (!renderManager) { return nullptr; }
            return static_cast<LineRendererPipeline*>(
                renderManager->GetRenderer(RenderPassType::Line));
        }
    }

    void PhysicsFeature::Initialize(SceneContext& ctx)
    {
        ApplyCVars();
        world_.Reset();

        // 速度と接触点の表示。物理側はレンダラを知らず、描く側が見に行く
        debugRenderer_ = std::make_unique<PhysicsDebugRenderer>();
        debugRenderer_->SetWorld(&world_);
        if (auto* const pipeline = GetLinePipeline(ctx)) {
            pipeline->RegisterLineSource(debugRenderer_.get());
        }

#ifdef CORE_EDITOR
        EnsureSettingsPanelRegistered(ctx.engine);
        SetActiveForSettingsPanel(this);
#else
        (void)ctx;
#endif
    }

    void PhysicsFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::PostObjectUpdate) {
            return;
        }

        ApplyCVars();

        if (!cvEnabled.Get()) {
            return;
        }

        CollectBodies(ctx);

        // timeScale を掛けた経過時間を積む（ヒットストップ・スローがそのまま効く）
        world_.Advance(Time::DeltaTime());
    }

    void PhysicsFeature::CollectBodies(SceneContext& ctx)
    {
        world_.ClearBodies();
        world_.SetCollisionWorld(nullptr);

        if (!ctx.gameObjectManager) {
            return;
        }

        CollisionWorld* const collisionWorld = FindCollisionWorld(ctx);

        if (collisionWorld) {
            // 前のフレームに立てた印とぶつかった強さを落としてから、剛体を持つものへ立て直す
            for (Collider* collider : collisionWorld->GetAllColliders()) {
                collider->SetSimulated(false);
                collider->ClearImpulse();
            }
        }

        ctx.gameObjectManager->ForEachComponent<RigidbodyComponent>(
            [this](RigidbodyComponent& body, GameObject& owner) {
                // 質量やスケールの変更に追従させるため、集めるたびに計算し直す
                body.RefreshInertia();
                world_.RegisterBody(&body);

                if (auto* const colliders = owner.GetComponent<ColliderComponent>()) {
                    colliders->ForEachEnabled(
                        [](Collider& collider) { collider.SetSimulated(true); });
                }
            });

        world_.SetCollisionWorld(collisionWorld);

        // スクリプトの FixedUpdate は物理のステップごとに回す（力の積み方がフレームレートに依らない）
        GameObjectManager* const manager = ctx.gameObjectManager;
        world_.SetPreStepCallback([manager](float) {
            manager->ForEachComponent<ScriptComponent>(
                [](ScriptComponent& script) { script.FixedUpdate(); });
            });
    }

    CollisionWorld* PhysicsFeature::FindCollisionWorld(SceneContext& ctx)
    {
        Scene* const scene = ctx.sceneManager
            ? dynamic_cast<Scene*>(ctx.sceneManager->GetCurrentScene())
            : nullptr;
        CollisionFeature* const collision = scene ? scene->GetFeature<CollisionFeature>() : nullptr;

        // 判定はこの後の CollisionFeature が行うので、ここで今フレームの登録を作っておく
        return (collision && ctx.gameObjectManager)
            ? &collision->GetQueryWorld(*ctx.gameObjectManager)
            : nullptr;
    }

    void PhysicsFeature::Finalize(SceneContext& ctx)
    {
        // 登録したまま消すとダングリングするので、破棄の前に外す
        if (auto* const pipeline = GetLinePipeline(ctx)) {
            pipeline->UnregisterLineSource(debugRenderer_.get());
        }
        debugRenderer_.reset();

        world_.Reset();

#ifdef CORE_EDITOR
        if (s_activePhysics == this) {
            SetActiveForSettingsPanel(nullptr);
        }
#endif
    }

    Vector3 PhysicsFeature::GetGravity()
    {
        return cvGravity.Get();
    }

    void PhysicsFeature::SetGravity(const Vector3& gravity)
    {
        cvGravity.Set(gravity);
    }

    void PhysicsFeature::ApplyCVars()
    {
        world_.SetGravity(cvGravity.Get());
        world_.SetFixedDeltaTime(cvFixedStep.Get());
        world_.SetMaxSubSteps(cvMaxSubSteps.Get());
        world_.SetCorrectionRate(cvCorrectionRate.Get());
        world_.SetPenetrationSlop(cvPenetrationSlop.Get());
        world_.SetSolverIterations(cvSolverIterations.Get());
        world_.SetRestitutionThreshold(cvRestitutionThreshold.Get());
        world_.SetSleepEnabled(cvSleepEnabled.Get());
        world_.SetSleepThresholds(cvSleepLinear.Get(), cvSleepAngular.Get(), cvTimeToSleep.Get());

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
        ImGui::Text("剛体 %d 個（眠り %d）/ 接触 %d 件",
            static_cast<int>(world_.GetBodyCount()),
            static_cast<int>(world_.GetSleepingCount()),
            static_cast<int>(world_.GetContactCount()));
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
