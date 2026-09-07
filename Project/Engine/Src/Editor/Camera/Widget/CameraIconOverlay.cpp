#include "pch.h"
#include "CameraIconOverlay.h"

#ifdef USE_IMGUI

#include "Editor/ImGui/ImGuiAll.h"
#include "Camera/Camera.h"
#include "Math/MathCore.h"

#include <cmath>

namespace CoreEngine
{
    namespace
    {
        /// @brief 標準の大きさ [px]（Unity のギズモアイコンと同じくらいの見え方）
        constexpr float kBaseSize = 22.0f;

        /// @brief 点を中心まわりに回して画面座標へ置く
        ImVec2 Rotate(float localX, float localY, float sin, float cos, float centerX, float centerY)
        {
            return ImVec2(
                centerX + localX * cos - localY * sin,
                centerY + localX * sin + localY * cos);
        }
    }

    bool CameraIconOverlay::WorldToScreen(const Vector3& world, const Camera& camera,
        const CameraEditorViewport& viewport, float& outX, float& outY)
    {
        if (viewport.width <= 0.0f || viewport.height <= 0.0f) {
            return false;
        }

        const Matrix4x4 viewProjection = camera.GetViewMatrix() * camera.GetProjectionMatrix();
        const Vector4 clip = MathCore::CoordinateTransform::TransformCoord(
            Vector4{ world.x, world.y, world.z, 1.0f }, viewProjection);

        // w <= 0 はカメラの後ろ。割ると符号が反転して、背後の点が画面内へ出てしまう。
        if (clip.w <= 1.0e-5f) {
            return false;
        }

        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;

        outX = viewport.x + (ndcX * 0.5f + 0.5f) * viewport.width;
        // NDC の Y は上が正、画面座標は下が正なので反転する。
        outY = viewport.y + (1.0f - (ndcY * 0.5f + 0.5f)) * viewport.height;
        return true;
    }

    float CameraIconOverlay::ComputeScreenAngle(const Vector3& position, const Vector3& forward,
        const Camera& camera, const CameraEditorViewport& viewport)
    {
        float baseX = 0.0f;
        float baseY = 0.0f;
        float tipX = 0.0f;
        float tipY = 0.0f;

        if (!WorldToScreen(position, camera, viewport, baseX, baseY)
            || !WorldToScreen(position + forward, camera, viewport, tipX, tipY)) {
            return 0.0f;
        }

        const float dx = tipX - baseX;
        const float dy = tipY - baseY;

        // ほぼ正面 / 真後ろから見ているときは画面上の向きが決まらない。
        // 無理に角度を出すとフレームごとにアイコンが回ってしまうので、既定の向きにする。
        if ((dx * dx + dy * dy) < 4.0f) {
            return 0.0f;
        }

        return std::atan2(dy, dx);
    }

    float CameraIconOverlay::GetHitRadius(float scale)
    {
        return kBaseSize * 0.5f * scale;
    }

    void CameraIconOverlay::DrawCameraGlyph(ImDrawList* draw, float screenX, float screenY,
        float forwardAngle, float scale, unsigned int fill, unsigned int outline)
    {
        if (!draw) {
            return;
        }

        const float size = kBaseSize * scale;
        const float sin = std::sin(forwardAngle);
        const float cos = std::cos(forwardAngle);

        // ビデオカメラの形。本体の箱 + レンズの台形。小さくても「カメラ」と読める
        // 輪郭を選んである（丸だとキーの点と区別がつかない）。
        const float bodyHalfW = size * 0.28f;
        const float bodyHalfH = size * 0.22f;
        const float lensLength = size * 0.26f;
        const float lensHalfNear = size * 0.13f;
        const float lensHalfFar = size * 0.22f;

        const ImVec2 body[4] = {
            Rotate(-bodyHalfW, -bodyHalfH, sin, cos, screenX, screenY),
            Rotate( bodyHalfW, -bodyHalfH, sin, cos, screenX, screenY),
            Rotate( bodyHalfW,  bodyHalfH, sin, cos, screenX, screenY),
            Rotate(-bodyHalfW,  bodyHalfH, sin, cos, screenX, screenY)
        };

        const ImVec2 lens[4] = {
            Rotate(bodyHalfW,               -lensHalfNear, sin, cos, screenX, screenY),
            Rotate(bodyHalfW + lensLength,  -lensHalfFar,  sin, cos, screenX, screenY),
            Rotate(bodyHalfW + lensLength,   lensHalfFar,  sin, cos, screenX, screenY),
            Rotate(bodyHalfW,                lensHalfNear, sin, cos, screenX, screenY)
        };

        // 影を先に敷く。明るい床の上でも輪郭が読めるようにするため。
        const ImU32 shadow = IM_COL32(0, 0, 0, 110);
        draw->AddRectFilled(
            ImVec2(screenX - size * 0.5f + 1.0f, screenY - size * 0.32f + 1.0f),
            ImVec2(screenX + size * 0.5f + 1.0f, screenY + size * 0.32f + 1.0f),
            shadow, size * 0.14f);

        draw->AddConvexPolyFilled(lens, 4, fill);
        draw->AddConvexPolyFilled(body, 4, fill);
        draw->AddPolyline(lens, 4, outline, ImDrawFlags_Closed, 1.5f);
        draw->AddPolyline(body, 4, outline, ImDrawFlags_Closed, 1.5f);

        // 本体の上に小さなリールを 1 つ。カメラらしさが一段上がる。
        const ImVec2 reel = Rotate(-bodyHalfW * 0.25f, -bodyHalfH - size * 0.09f,
            sin, cos, screenX, screenY);
        draw->AddCircleFilled(reel, size * 0.09f, fill);
        draw->AddCircle(reel, size * 0.09f, outline, 0, 1.2f);
    }
}

#endif // USE_IMGUI
