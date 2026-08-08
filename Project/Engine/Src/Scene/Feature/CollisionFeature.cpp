#include "pch.h"
#include "CollisionFeature.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Graphics/Render/RenderManager.h"
#include "Utility/CVar/CVar.h"

#ifdef USE_IMGUI
#include "Collision/Debug/CollisionMatrixPanel.h"
#endif

namespace CoreEngine
{
    namespace {
        // CVar はファイルスコープの static で定義する（レジストリへの登録が起動時に済む）

        /// 0 = BruteForce（既定）/ 1 = UniformGrid
        CVar<int> cvBroadPhase{
            "r.Collision.BroadPhase", 0,
            "ブロードフェーズ: 0=総当たり / 1=一様グリッド", CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvGridCellSize{
            "r.Collision.GridCellSize", 8.0f,
            "一様グリッドのセルサイズ（よくある物体サイズの 1〜2 倍が目安）",
            CVarRange{ 0.5f, 64.0f } };
    }

    namespace {
        /// Line パスのパイプラインを取得する（無ければ nullptr）
        LineRendererPipeline* GetLinePipeline(SceneContext& ctx)
        {
            auto* renderManager = ctx.engine ? ctx.engine->GetService<RenderManager>() : nullptr;
            if (!renderManager) { return nullptr; }
            return static_cast<LineRendererPipeline*>(
                renderManager->GetRenderer(RenderPassType::Line));
        }
    }

    void CollisionFeature::Initialize(SceneContext& ctx)
    {
        // コライダーのワイヤ表示。Collider 側はレンダラを知らず、描く側が見に行く。
        // GameObject ではなく ILineSource として Line パスへ登録する（Hierarchy には出ない）。
        debugRenderer_ = std::make_unique<ColliderDebugRenderer>();
        debugRenderer_->SetWorld(&collisionWorld_);
        if (auto* pipeline = GetLinePipeline(ctx)) {
            pipeline->RegisterLineSource(debugRenderer_.get());
        }

#ifdef USE_IMGUI
        // コリジョンマトリクス編集ウィンドウ（Engine Settings）。編集対象を現在のシーンへ向ける。
        CollisionMatrixPanel::EnsureRegistered(ctx.engine);
        CollisionMatrixPanel::SetActiveConfig(&collisionConfig_);
#endif
    }

    void CollisionFeature::ApplyCVars()
    {
        const auto desired = (cvBroadPhase.Get() == 1)
            ? CollisionWorld::BroadPhaseType::UniformGrid
            : CollisionWorld::BroadPhaseType::BruteForce;

        if (collisionWorld_.GetBroadPhaseType() != desired) {
            collisionWorld_.SetBroadPhase(desired);
        }
        collisionWorld_.SetGridCellSize(cvGridCellSize.Get());
    }

    void CollisionFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::PostObjectUpdate) {
            return;
        }

        ApplyCVars();

        // コリジョン判定（毎フレーム: 収集 → 判定）
        // ClearColliders() でコライダーリストのみリセット（衝突履歴は保持する）
        collisionWorld_.ClearColliders();
        ctx.gameObjectManager->RegisterAllColliders(&collisionWorld_);
        collisionWorld_.Step();
    }

    void CollisionFeature::Finalize(SceneContext& ctx)
    {
#ifdef USE_IMGUI
        // シーンと一緒に消える CollisionConfig を UI が指したままにしない
        CollisionMatrixPanel::SetActiveConfig(nullptr);
#endif
        // 破棄前に Line パスの登録を外す（登録したまま消すとダングリング）
        if (auto* pipeline = GetLinePipeline(ctx)) {
            pipeline->UnregisterLineSource(debugRenderer_.get());
        }
        debugRenderer_.reset();
        collisionWorld_.Clear();
    }
}
