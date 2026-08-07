#include "pch.h"
#include "ColliderDebugRenderer.h"

#include "Collision/CollisionWorld.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Line/LineManager.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Graphics/Render/RenderManager.h"
#include "Utility/CVar/CVar.h"

#include <vector>

namespace CoreEngine
{
    namespace {
        // CVar はファイルスコープの static で定義する（レジストリへの登録が起動時に済む）

        CVar<bool> cvDebugDraw{
            "r.Collision.DebugDraw", false,
            "コライダーのワイヤ表示" };

        CVar<bool> cvDrawOnlyColliding{
            "r.Collision.DebugDrawOnlyColliding", false,
            "接触中のコライダーだけを描く" };

        CVar<Vector3> cvColorIdle{
            "r.Collision.DebugColorIdle", Vector3{ 0.25f, 0.85f, 0.35f },
            "非接触のコライダーの線色" };

        CVar<Vector3> cvColorHit{
            "r.Collision.DebugColorHit", Vector3{ 0.95f, 0.30f, 0.20f },
            "接触中のコライダーの線色" };

        CVar<Vector3> cvColorTrigger{
            "r.Collision.DebugColorTrigger", Vector3{ 0.30f, 0.65f, 0.95f },
            "トリガー（通知専用）のコライダーの線色" };

        CVar<float> cvLineAlpha{
            "r.Collision.DebugLineAlpha", 0.85f,
            "コライダー線の不透明度", CVarRange{ 0.0f, 1.0f } };

        CVar<int> cvSphereSegments{
            "r.Collision.DebugSphereSegments", 16,
            "球コライダーの分割数", CVarRange{ 4.0f, 48.0f } };
    }

    void ColliderDebugRenderer::Draw(const Camera* camera)
    {
        (void)camera;

        if (!cvDebugDraw.Get() || !world_) {
            return;
        }

        auto* engine = GetEngineSystem();
        if (!engine) { return; }
        auto* renderManager = engine->GetService<RenderManager>();
        if (!renderManager) { return; }

        auto* pipeline = static_cast<LineRendererPipeline*>(
            renderManager->GetRenderer(RenderPassType::Line));
        if (!pipeline) { return; }

        const float alpha = cvLineAlpha.Get();
        const int   segments = cvSphereSegments.Get();
        const bool  onlyColliding = cvDrawOnlyColliding.Get();

        std::vector<Line> lines;

        for (const Collider* collider : world_->GetAllColliders()) {
            if (!collider || !collider->IsEnabled()) { continue; }

            const bool touching = world_->IsColliding(*collider);
            if (onlyColliding && !touching) { continue; }

            const Vector3 color = touching ? cvColorHit.Get()
                : (collider->IsTrigger() ? cvColorTrigger.Get() : cvColorIdle.Get());

            if (collider->GetShapeType() == ColliderShapeType::Sphere) {
                const Geometry::Sphere sphere = collider->GetWorldSphere();
                auto shape = LineManager::GenerateSphereLines(
                    sphere.center, sphere.radius, color, alpha, segments);
                lines.insert(lines.end(), shape.begin(), shape.end());
            } else {
                const Geometry::AABB box = collider->GetWorldAABB();
                auto shape = LineManager::GenerateBoxLines(
                    box.GetCenter(), box.GetSize(), color, alpha);
                lines.insert(lines.end(), shape.begin(), shape.end());
            }
        }

        if (!lines.empty()) {
            pipeline->AddLines(lines);
        }
    }
}
