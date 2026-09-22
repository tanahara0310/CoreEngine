#include "pch.h"
#include "PhysicsDebugRenderer.h"

#include "GameObject/GameObject.h"
#include "Graphics/Line/LineManager.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/RigidbodyComponent.h"
#include "Utility/CVar/CVar.h"

#include <vector>

namespace CoreEngine
{
    namespace {
        // CVar はファイルスコープの static で定義する（レジストリへの登録が起動時に済む）

        CVar<bool> cvDebugDraw{
            "sys.Physics.DebugDraw", false,
            "剛体の速度と接触点を線で表示する" };

        CVar<bool> cvDrawVelocity{
            "sys.Physics.DebugDrawVelocity", true,
            "速度と角速度の向きを描く" };

        CVar<bool> cvDrawContacts{
            "sys.Physics.DebugDrawContacts", true,
            "接触点と法線を描く" };

        CVar<float> cvVelocityScale{
            "sys.Physics.DebugVelocityScale", 0.15f,
            "速度 1m/s を何 m の線で描くか", CVarRange{ 0.01f, 2.0f } };

        CVar<float> cvNormalLength{
            "sys.Physics.DebugNormalLength", 0.4f,
            "接触法線の長さ（m）", CVarRange{ 0.05f, 3.0f } };

        CVar<float> cvMarkerSize{
            "sys.Physics.DebugMarkerSize", 0.12f,
            "接触点と眠りの印の大きさ（m）", CVarRange{ 0.02f, 1.0f } };

        CVar<Vector3> cvColorVelocity{
            "sys.Physics.DebugColorVelocity", Vector3{ 0.30f, 0.95f, 0.45f },
            "速度の線の色" };

        CVar<Vector3> cvColorSpin{
            "sys.Physics.DebugColorSpin", Vector3{ 0.95f, 0.85f, 0.25f },
            "角速度の線の色" };

        CVar<Vector3> cvColorContact{
            "sys.Physics.DebugColorContact", Vector3{ 0.95f, 0.45f, 0.15f },
            "接触点と法線の色" };

        CVar<Vector3> cvColorSleeping{
            "sys.Physics.DebugColorSleeping", Vector3{ 0.45f, 0.50f, 0.60f },
            "眠っている剛体の印の色" };

        CVar<float> cvLineAlpha{
            "sys.Physics.DebugLineAlpha", 0.9f,
            "線の不透明度", CVarRange{ 0.0f, 1.0f } };

        /// @brief 3 軸に伸びる十字を足す
        void AddCross(std::vector<Line>& lines, const Vector3& center, float size,
                      const Vector3& color, float alpha)
        {
            const Vector3 axisX{ size, 0.0f, 0.0f };
            const Vector3 axisY{ 0.0f, size, 0.0f };
            const Vector3 axisZ{ 0.0f, 0.0f, size };

            lines.push_back({ center - axisX, center + axisX, color, alpha });
            lines.push_back({ center - axisY, center + axisY, color, alpha });
            lines.push_back({ center - axisZ, center + axisZ, color, alpha });
        }
    }

    void PhysicsDebugRenderer::SubmitLines(LineRendererPipeline& pipeline, const Camera* camera)
    {
        (void)camera;

        if (!cvDebugDraw.Get() || !world_) {
            return;
        }

        const float alpha = cvLineAlpha.Get();
        const float markerSize = cvMarkerSize.Get();

        std::vector<Line> lines;

        if (cvDrawVelocity.Get()) {
            const float velocityScale = cvVelocityScale.Get();
            const Vector3 velocityColor = cvColorVelocity.Get();
            const Vector3 spinColor = cvColorSpin.Get();
            const Vector3 sleepingColor = cvColorSleeping.Get();

            for (const RigidbodyComponent* body : world_->GetBodies()) {
                const GameObject* const owner = body ? body->GetOwner() : nullptr;
                if (!owner) { continue; }

                const Vector3 origin = owner->GetWorldPosition();

                // 眠っているものは印だけにする（速度は 0 なので線が出ない）
                if (body->IsSleeping()) {
                    AddCross(lines, origin, markerSize, sleepingColor, alpha);
                    continue;
                }

                const Vector3 velocity = body->GetVelocity();
                if (LengthSquared(velocity) > 0.0f) {
                    lines.push_back({ origin, origin + velocity * velocityScale,
                                      velocityColor, alpha });
                }

                const Vector3 spin = body->GetAngularVelocity();
                if (LengthSquared(spin) > 0.0f) {
                    lines.push_back({ origin, origin + spin * velocityScale, spinColor, alpha });
                }
            }
        }

        if (cvDrawContacts.Get()) {
            const Vector3 contactColor = cvColorContact.Get();
            const float normalLength = cvNormalLength.Get();

            for (const ContactConstraint& constraint : world_->GetConstraints()) {
                AddCross(lines, constraint.point, markerSize * 0.6f, contactColor, alpha);
                lines.push_back({ constraint.point,
                                  constraint.point + constraint.normal * normalLength,
                                  contactColor, alpha });
            }
        }

        if (!lines.empty()) {
            pipeline.AddLines(lines);
        }
    }
}
