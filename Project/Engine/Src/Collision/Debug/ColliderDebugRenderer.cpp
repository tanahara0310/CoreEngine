#include "pch.h"
#include "ColliderDebugRenderer.h"

#include "Camera/Camera.h"
#include "Collision/CollisionWorld.h"
#include "Graphics/Line/LineManager.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Utility/CVar/CVar.h"

#include <algorithm>
#include <vector>

namespace CoreEngine
{
    namespace {
        // CVar はファイルスコープの static で定義する（レジストリへの登録が起動時に済む）

        CVar<bool> cvDebugDraw{
            "d.Collision.DebugDraw", false,
            "コライダーのワイヤ表示" };

        CVar<bool> cvDrawOnlyColliding{
            "d.Collision.DebugDrawOnlyColliding", false,
            "接触中のコライダーだけを描く" };

        CVar<Vector3> cvColorIdle{
            "d.Collision.DebugColorIdle", Vector3{ 0.25f, 0.85f, 0.35f },
            "非接触のコライダーの線色" };

        CVar<Vector3> cvColorHit{
            "d.Collision.DebugColorHit", Vector3{ 0.95f, 0.30f, 0.20f },
            "接触中のコライダーの線色" };

        CVar<Vector3> cvColorTrigger{
            "d.Collision.DebugColorTrigger", Vector3{ 0.30f, 0.65f, 0.95f },
            "トリガー（通知専用）のコライダーの線色" };

        CVar<float> cvLineAlpha{
            "d.Collision.DebugLineAlpha", 0.85f,
            "コライダー線の不透明度", CVarRange{ 0.0f, 1.0f } };

        CVar<int> cvSphereSegments{
            "d.Collision.DebugSphereSegments", 16,
            "球コライダーの分割数", CVarRange{ 4.0f, 48.0f } };

        CVar<float> cvMeshRadius{
            "d.Collision.DebugMeshRadius", 30.0f,
            "メッシュのコライダーの三角形を描く、カメラからの距離（m）", CVarRange{ 1.0f, 500.0f } };

        CVar<int> cvMeshMaxTriangles{
            "d.Collision.DebugMeshMaxTriangles", 6000,
            "メッシュのコライダー 1 つで描く三角形の数の上限", CVarRange{ 100.0f, 20000.0f } };

        /// @brief メッシュの三角形を描く範囲の中心（カメラが無ければ形の中心）
        Vector3 g_meshFocus{ 0.0f, 0.0f, 0.0f };
        bool    g_hasMeshFocus = false;

        /// @brief 球の枠の線を作る
        std::vector<Line> GenerateShapeLines(const Geometry::Sphere& sphere,
            const Vector3& color, float alpha, int segments)
        {
            return LineManager::GenerateSphereLines(
                sphere.center, sphere.radius, color, alpha, segments);
        }

        /// @brief カプセルの枠の線を作る
        std::vector<Line> GenerateShapeLines(const Geometry::Capsule& capsule,
            const Vector3& color, float alpha, int segments)
        {
            return LineManager::GenerateCapsuleLines(
                capsule.start, capsule.end, capsule.radius, color, alpha, segments);
        }

        /// @brief メッシュの三角形の線を作る（カメラの近くだけ・数に上限）
        std::vector<Line> GenerateShapeLines(const Geometry::MeshView& mesh,
            const Vector3& color, float alpha, int /*segments*/)
        {
            std::vector<Line> lines;
            if (!mesh.IsValid()) {
                return lines;
            }
            const Geometry::AABB bounds = mesh.WorldBounds();
            const Vector3 focus = g_hasMeshFocus ? g_meshFocus : bounds.GetCenter();
            const float radius = cvMeshRadius.Get();
            const Vector3 reach{ radius, radius, radius };
            const Geometry::AABB query = mesh.LocalBoxOf(Geometry::AABB(focus - reach, focus + reach));

            const int limit = cvMeshMaxTriangles.Get();
            int drawn = 0;
            lines.reserve(static_cast<size_t>((std::min)(limit, 2048)) * 3);
            mesh.mesh->ForEachTriangleInBox(query, [&](uint32_t triangle) {
                if (drawn >= limit) {
                    return;
                }
                ++drawn;
                Vector3 a, b, c;
                mesh.WorldTriangle(triangle, a, b, c);
                lines.push_back(Line{ a, b, color, alpha, true });
                lines.push_back(Line{ b, c, color, alpha, true });
                lines.push_back(Line{ c, a, color, alpha, true });
            });
            return lines;
        }

        /// @brief 向き付きの箱の枠の線を作る（分割数は使わない）
        std::vector<Line> GenerateShapeLines(const Geometry::OBB& box,
            const Vector3& color, float alpha, int /*segments*/)
        {
            return LineManager::GenerateBoxLines(
                box.center, box.halfExtents * 2.0f,
                box.axes[0], box.axes[1], box.axes[2], color, alpha);
        }
    }

    void ColliderDebugRenderer::SubmitLines(LineRendererPipeline& pipeline, const Camera* camera)
    {
        g_hasMeshFocus = camera != nullptr;
        if (camera) {
            g_meshFocus = camera->GetPosition();
        }

        if (!cvDebugDraw.Get() || !worldProvider_) {
            return;
        }
        const CollisionWorld* const world = worldProvider_();
        if (!world) {
            return;
        }

        const float alpha = cvLineAlpha.Get();
        const int   segments = cvSphereSegments.Get();
        const bool  onlyColliding = cvDrawOnlyColliding.Get();

        std::vector<Line> lines;

        for (const Collider* collider : world->GetAllColliders()) {
            if (!collider || !collider->IsEnabled()) { continue; }

            const bool touching = world->IsColliding(*collider);
            if (onlyColliding && !touching) { continue; }

            const Vector3 color = touching ? cvColorHit.Get()
                : (collider->IsTrigger() ? cvColorTrigger.Get() : cvColorIdle.Get());

            const auto shape = VisitWorldShape(*collider, [&](const auto& worldShape) {
                return GenerateShapeLines(worldShape, color, alpha, segments);
            });
            lines.insert(lines.end(), shape.begin(), shape.end());
        }

        if (!lines.empty()) {
            pipeline.AddLines(lines);
        }
    }
}
