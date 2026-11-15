#include "pch.h"
#include "Editor/Environment/SunMoonSceneGizmo.h"

#ifdef CORE_EDITOR

#include "Camera/Camera.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/Gizmo.h"
#include "Editor/Scene/SceneViewTools.h"
#include "GameObject/GameObject.h"
#include "Graphics/Render/SkyBox/SkyBoxComponent.h"
#include "Math/MathCore.h"
#include "Utility/CVar/CVar.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        namespace Theme = Editor::Theme;
        namespace Overlay = Editor::SceneOverlay;

        CVar<bool> cvSceneGizmo{
            "d.Atmosphere.SceneGizmo", true,
            "空の物か太陽・月のライトを選んでいる間、シーンビューに太陽と月の位置と軌道を描く" };

        constexpr float kDegToRad = MathCore::Constants::kDegToRad;

        /// カメラから空の印までの距離 [m]
        constexpr float kSkyDistance = 500.0f;

        /// 地平線の円を分ける角度 [deg]
        constexpr float kHorizonStepDeg = 5.0f;

        /// 太陽の軌道を分ける数（1 日を 15 分ごと）
        constexpr int kPathSamples = 96;

        /// 印を掴める半径 [px]
        constexpr float kGrabRadius = 16.0f;

        /// 画面の外の天体の矢印を、シーンビューの縁から離す幅 [px]
        constexpr float kEdgeInset = 28.0f;

        /// Ctrl を押したときに止まる角度 [deg]
        constexpr float kSnapDeg = 5.0f;

        ImU32 Color(int r, int g, int b, float alpha = 1.0f)
        {
            return ImGui::GetColorU32(Theme::FromSrgb(r, g, b, alpha));
        }

        ImU32 Color(const ImVec4& color, float alpha)
        {
            return ImGui::GetColorU32(Theme::WithAlpha(color, alpha));
        }

        /// @brief 影つきの文字（alignX は 0 で左端・0.5 で中心を合わせる）
        void ShadowText(ImDrawList& drawList, ImVec2 position, ImU32 color, const char* text, float alignX = 0.0f)
        {
            const ImVec2 extent = ImGui::CalcTextSize(text);
            position.x -= extent.x * alignX;
            position.y -= extent.y * 0.5f;
            drawList.AddText(ImVec2(position.x + 1.0f, position.y + 1.0f), IM_COL32(0, 0, 0, 190), text);
            drawList.AddText(position, color, text);
        }

        const char* BodyName(SkyBody body)
        {
            return body == SkyBody::Sun ? "太陽" : "月";
        }

        /// @brief シーンビューの画面の中か
        bool IsInside(const Editor::SceneViewContext& context, const ImVec2& point)
        {
            return point.x >= context.position.x && point.y >= context.position.y
                && point.x <= context.position.x + context.size.x && point.y <= context.position.y + context.size.y;
        }

        /// @brief 空の上の天体の、シーンビューでの見え方
        struct BodyOnScreen
        {
            SkyBody body = SkyBody::Sun;
            SkyAngles angles{};
            bool visible = false; ///< 画面の中に見えているか
            ImVec2 screen{};      ///< 見えているときの位置、見えていないときは縁の矢印の位置
            ImVec2 arrow{};       ///< 縁の矢印の向き（画面の外のとき）
        };

        /// @brief カメラから見た天体の位置（画面の外なら縁の矢印の位置と向き）
        BodyOnScreen Locate(const Editor::SceneViewContext& context, const Camera& camera, SkyBody body, const SkyAngles& angles)
        {
            BodyOnScreen result;
            result.body = body;
            result.angles = angles;

            const Vector3 direction = SkyAnglesToDirection(angles);
            ImVec2 screen;
            if (Overlay::WorldToScreen(camera.GetPosition() + direction * kSkyDistance, screen) && IsInside(context, screen)) {
                result.visible = true;
                result.screen = screen;
                return result;
            }

            // 画面の上の向き（カメラの右と上への成分）。真後ろは下へ向ける
            float x = Dot(direction, camera.GetRight());
            float y = -Dot(direction, camera.GetUp());
            const float length = std::sqrt(x * x + y * y);
            if (length < 1.0e-4f) {
                x = 0.0f;
                y = 1.0f;
            } else {
                x /= length;
                y /= length;
            }
            result.arrow = ImVec2(x, y);

            // 中心からその向きへ伸ばし、縁から少し内側の四角に当たる点
            const ImVec2 center(context.position.x + context.size.x * 0.5f, context.position.y + context.size.y * 0.5f);
            const float halfWidth = (std::max)(1.0f, context.size.x * 0.5f - kEdgeInset);
            const float halfHeight = (std::max)(1.0f, context.size.y * 0.5f - kEdgeInset);
            const float scaleX = std::abs(x) > 1.0e-4f ? halfWidth / std::abs(x) : FLT_MAX;
            const float scaleY = std::abs(y) > 1.0e-4f ? halfHeight / std::abs(y) : FLT_MAX;
            const float scale = (std::min)(scaleX, scaleY);
            result.screen = ImVec2(center.x + x * scale, center.y + y * scale);
            return result;
        }

        // ===== 色（sRGB） =====
        ImU32 SunColor(bool aboveHorizon, float alpha = 1.0f)
        {
            return aboveHorizon ? Color(255, 198, 72, alpha) : Color(170, 120, 70, alpha);
        }

        ImU32 MoonColor(bool aboveHorizon, float alpha = 1.0f)
        {
            return aboveHorizon ? Color(222, 230, 248, alpha) : Color(120, 128, 150, alpha);
        }

        void DrawSunMark(ImDrawList& drawList, const ImVec2& at, bool aboveHorizon)
        {
            drawList.AddCircleFilled(at, 16.0f, SunColor(true, aboveHorizon ? 0.22f : 0.10f), 32);
            if (aboveHorizon) {
                for (int i = 0; i < 8; ++i) {
                    const float a = static_cast<float>(i) * 45.0f * kDegToRad;
                    const ImVec2 dir(std::cos(a), std::sin(a));
                    drawList.AddLine(ImVec2(at.x + dir.x * 11.0f, at.y + dir.y * 11.0f),
                        ImVec2(at.x + dir.x * 15.5f, at.y + dir.y * 15.5f), SunColor(true), 2.0f);
                }
            }
            drawList.AddCircleFilled(at, 8.0f, SunColor(aboveHorizon), 24);
            drawList.AddCircle(at, 8.0f, IM_COL32(20, 18, 24, 200), 24, 1.5f);
        }

        void DrawMoonMark(ImDrawList& drawList, const ImVec2& at, bool aboveHorizon)
        {
            drawList.AddCircleFilled(at, 13.0f, Color(180, 200, 240, aboveHorizon ? 0.20f : 0.08f), 32);
            drawList.AddCircleFilled(at, 7.5f, MoonColor(aboveHorizon), 24);
            drawList.AddCircleFilled(ImVec2(at.x - 2.0f, at.y - 1.5f), 1.6f, Color(168, 178, 202), 12);
            drawList.AddCircleFilled(ImVec2(at.x + 2.4f, at.y + 2.0f), 1.2f, Color(168, 178, 202), 12);
            drawList.AddCircle(at, 7.5f, IM_COL32(20, 18, 24, 200), 24, 1.5f);
        }

        void DrawMark(ImDrawList& drawList, SkyBody body, const ImVec2& at, bool aboveHorizon)
        {
            if (body == SkyBody::Sun) {
                DrawSunMark(drawList, at, aboveHorizon);
            } else {
                DrawMoonMark(drawList, at, aboveHorizon);
            }
        }

        /// @brief 画面の外の天体を、縁の矢印と小さな印で示す
        void DrawEdgeArrow(ImDrawList& drawList, const BodyOnScreen& body)
        {
            const ImVec2 at = body.screen;
            const ImVec2 dir = body.arrow;
            const ImVec2 side(-dir.y, dir.x);
            const ImVec2 tip(at.x + dir.x * 20.0f, at.y + dir.y * 20.0f);
            const ImVec2 baseA(at.x + dir.x * 10.0f + side.x * 7.0f, at.y + dir.y * 10.0f + side.y * 7.0f);
            const ImVec2 baseB(at.x + dir.x * 10.0f - side.x * 7.0f, at.y + dir.y * 10.0f - side.y * 7.0f);
            const bool aboveHorizon = body.angles.elevationDeg >= 0.0f;
            const ImU32 color = body.body == SkyBody::Sun ? SunColor(aboveHorizon) : MoonColor(aboveHorizon);
            drawList.AddTriangleFilled(tip, baseA, baseB, color);
            drawList.AddTriangle(tip, baseA, baseB, IM_COL32(20, 18, 24, 200), 1.2f);
            DrawMark(drawList, body.body, at, aboveHorizon);
        }

        /// @brief 空の上の点を順に結ぶ（地平線の上と下で濃さを変える）
        void DrawSkyPath(const Camera& camera, const std::vector<SkyAngles>& points, ImU32 above, ImU32 below, float thickness)
        {
            const Vector3 eye = camera.GetPosition();
            std::vector<Vector3> run;
            bool runAbove = !points.empty() && points.front().elevationDeg >= 0.0f;
            const auto flush = [&]() {
                if (run.size() >= 2) {
                    Overlay::Polyline(run.data(), run.size(), false, runAbove ? above : below, thickness);
                }
            };
            for (const SkyAngles& point : points) {
                const bool isAbove = point.elevationDeg >= 0.0f;
                const Vector3 world = eye + SkyAnglesToDirection(point) * kSkyDistance;
                if (isAbove != runAbove) {
                    // 地平線をまたぐ所は両方の線へ同じ点を入れてつなぐ
                    run.push_back(world);
                    flush();
                    run.clear();
                    runAbove = isAbove;
                }
                run.push_back(world);
            }
            flush();
        }
    }

    bool SunMoonSceneGizmo::IsEnabled()
    {
        return cvSceneGizmo.Get();
    }

    void SunMoonSceneGizmo::SetEnabled(bool enabled)
    {
        cvSceneGizmo.Set(enabled);
    }

    void SunMoonSceneGizmo::Initialize(SunMoonController& controller)
    {
        controller_ = &controller;
        registration_ = Editor::SceneViewTools::Get().Register(
            [this](const Editor::SceneViewContext& context) { Draw(context); });
    }

    void SunMoonSceneGizmo::Draw(const Editor::SceneViewContext& context)
    {
        if (!controller_ || !context.camera || context.camera->IsOrthographic()) {
            dragging_.reset();
            return;
        }
        const Camera& camera = *context.camera;

        // カメラの向きと画角を渡す（空の図が見ている範囲を描くのに使う）
        SkyViewCamera view;
        view.forward = camera.GetForward();
        view.right = camera.GetRight();
        view.up = camera.GetUp();
        const Matrix4x4& projection = camera.GetUnjitteredProjectionMatrix();
        if (projection.m[0][0] > 0.0f && projection.m[1][1] > 0.0f) {
            view.tanHalfFovX = 1.0f / projection.m[0][0];
            view.tanHalfFovY = 1.0f / projection.m[1][1];
        }
        controller_->SetViewCamera(view);
        controller_->Tick(ImGui::GetIO().DeltaTime);

        if (!cvSceneGizmo.Get()) {
            dragging_.reset();
            return;
        }
        GameObject* const selected = context.selected && !context.selected->IsMarkedForDestroy() ? context.selected : nullptr;
        const bool skySelected = selected
            && (selected->GetComponent<SkyBoxComponent>() != nullptr || controller_->IsSkyLightObject(selected));
        if (!skySelected && !dragging_) {
            return;
        }

        controller_->SyncFromLights();
        ImDrawList& drawList = *ImGui::GetWindowDrawList();
        const Vector3 eye = camera.GetPosition();
        const auto skyPoint = [&](const SkyAngles& angles) { return eye + SkyAnglesToDirection(angles) * kSkyDistance; };

        // ===== 地平線と方角 =====
        {
            std::vector<Vector3> horizon;
            for (float a = 0.0f; a < 360.0f; a += kHorizonStepDeg) {
                horizon.push_back(skyPoint(SkyAngles{ 0.0f, a }));
            }
            Overlay::Polyline(horizon.data(), horizon.size(), true, Color(236, 240, 248, 0.55f), 1.5f);

            static constexpr const char* kNames[] = { "北", "北東", "東", "南東", "南", "南西", "西", "北西" };
            for (int i = 0; i < 8; ++i) {
                const float azimuth = static_cast<float>(i) * 45.0f;
                const bool cardinal = (i % 2) == 0;
                Overlay::Line(skyPoint(SkyAngles{ 0.0f, azimuth }), skyPoint(SkyAngles{ cardinal ? 4.0f : 2.0f, azimuth }),
                    Color(236, 240, 248, cardinal ? 0.7f : 0.45f), 1.5f);
                ImVec2 at;
                if (Overlay::WorldToScreen(skyPoint(SkyAngles{ cardinal ? 5.5f : 3.5f, azimuth }), at) && IsInside(context, at)) {
                    ShadowText(drawList, at, ImGui::GetColorU32(cardinal ? Theme::kText : Theme::kTextDim), kNames[i], 0.5f);
                }
            }
        }

        // ===== 太陽の 1 日の軌道 =====
        {
            std::vector<SkyAngles> path(kPathSamples + 1);
            for (int i = 0; i <= kPathSamples; ++i) {
                path[i] = controller_->SunAnglesAt(24.0f * static_cast<float>(i) / kPathSamples);
            }
            DrawSkyPath(camera, path, Color(255, 214, 150, 0.6f), Color(255, 214, 150, 0.2f), 1.5f);

            for (int hour = 3; hour < 24; hour += 3) {
                const SkyAngles angles = controller_->SunAnglesAt(static_cast<float>(hour));
                if (angles.elevationDeg < -2.0f) {
                    continue;
                }
                ImVec2 at;
                if (!Overlay::WorldToScreen(skyPoint(angles), at) || !IsInside(context, at)) {
                    continue;
                }
                drawList.AddCircleFilled(at, 2.5f, Color(255, 214, 150, 0.8f), 12);
                char text[16];
                std::snprintf(text, sizeof(text), "%d時", hour);
                ShadowText(drawList, ImVec2(at.x + 6.0f, at.y - 9.0f), Color(255, 214, 150, 0.9f), text);
            }
        }

        // ===== 天体 =====
        std::vector<BodyOnScreen> bodies;
        for (const SkyBody body : { SkyBody::Moon, SkyBody::Sun }) {
            if (controller_->IsShown(body)) {
                bodies.push_back(Locate(context, camera, body, controller_->GetAngles(body)));
            }
        }

        // 地平線から天体へ縦の線を引く
        for (const BodyOnScreen& body : bodies) {
            const SkyAngles foot{ 0.0f, body.angles.azimuthDeg };
            const ImU32 color = body.body == SkyBody::Sun ? SunColor(true, 0.45f) : MoonColor(true, 0.45f);
            std::vector<Vector3> arc;
            constexpr int kArcSteps = 12;
            for (int i = 0; i <= kArcSteps; ++i) {
                const float t = static_cast<float>(i) / kArcSteps;
                arc.push_back(skyPoint(SkyAngles{ body.angles.elevationDeg * t, foot.azimuthDeg }));
            }
            Overlay::Polyline(arc.data(), arc.size(), false, color, 1.2f);
        }

        // ===== マウス =====
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool canUseMouse = context.hovered && !Gizmo::IsOver() && !Gizmo::IsUsing();
        std::optional<SkyBody> hovered;
        if (canUseMouse && !dragging_) {
            float best = kGrabRadius * kGrabRadius;
            for (const BodyOnScreen& body : bodies) {
                if (!body.visible) {
                    continue;
                }
                const float dx = mouse.x - body.screen.x;
                const float dy = mouse.y - body.screen.y;
                if (dx * dx + dy * dy <= best) {
                    best = dx * dx + dy * dy;
                    hovered = body.body;
                }
            }
        }

        if (hovered && controller_->CanMove(*hovered)) {
            Editor::SceneViewTools::Get().RequestMouse();
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                dragging_ = hovered;
                controller_->BeginEdit();
            }
        } else if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
        }

        if (dragging_) {
            Editor::SceneViewTools::Get().RequestMouse();
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && controller_->CanMove(*dragging_)) {
                // カーソルの下を通る視線の向きを、空の上の位置にする
                const float ndcX = (mouse.x - context.position.x) / (std::max)(1.0f, context.size.x) * 2.0f - 1.0f;
                const float ndcY = 1.0f - (mouse.y - context.position.y) / (std::max)(1.0f, context.size.y) * 2.0f;
                const Vector3 ray = view.forward + view.right * (ndcX * view.tanHalfFovX) + view.up * (ndcY * view.tanHalfFovY);
                SkyAngles angles = DirectionToSkyAngles(ray, controller_->GetAngles(*dragging_).azimuthDeg);
                if (ImGui::GetIO().KeyCtrl) {
                    angles.elevationDeg = std::round(angles.elevationDeg / kSnapDeg) * kSnapDeg;
                    angles.azimuthDeg = WrapAzimuth(std::round(angles.azimuthDeg / kSnapDeg) * kSnapDeg);
                }
                controller_->MoveTo(*dragging_, angles);
                for (BodyOnScreen& body : bodies) {
                    if (body.body == *dragging_) {
                        body = Locate(context, camera, body.body, controller_->GetAngles(body.body));
                    }
                }
            } else {
                controller_->EndEdit();
                dragging_.reset();
            }
        }

        // 月 → 太陽の順（重なったら太陽が上）
        for (const BodyOnScreen& body : bodies) {
            const bool aboveHorizon = body.angles.elevationDeg >= 0.0f;
            if (!body.visible) {
                DrawEdgeArrow(drawList, body);
                continue;
            }
            DrawMark(drawList, body.body, body.screen, aboveHorizon);
            const bool highlighted = hovered == body.body || dragging_ == body.body;
            if (highlighted) {
                drawList.AddCircle(body.screen, body.body == SkyBody::Sun ? 19.0f : 16.0f,
                    ImGui::GetColorU32(Theme::kText), 32, 1.8f);
            }
            char text[48];
            std::snprintf(text, sizeof(text), "%s %.0f°", BodyName(body.body), body.angles.elevationDeg);
            ShadowText(drawList, ImVec2(body.screen.x + 20.0f, body.screen.y), body.body == SkyBody::Sun
                ? SunColor(true) : MoonColor(true), text);
        }

        // ===== 説明 =====
        const std::optional<SkyBody> focus = dragging_ ? dragging_ : hovered;
        if (focus) {
            const SkyAngles angles = controller_->GetAngles(*focus);
            ImGui::BeginTooltip();
            ImGui::Text("%s　高度 %.1f°　方位 %.1f°（%s）", BodyName(*focus), angles.elevationDeg, angles.azimuthDeg,
                CompassName(angles.azimuthDeg));
            if (!dragging_) {
                if (controller_->IsTimeDriven()) {
                    ImGui::TextDisabled(*focus == SkyBody::Sun
                        ? "ドラッグで軌道に沿って時刻を動かす" : "ドラッグで軌道に沿って月齢を動かす");
                } else {
                    ImGui::TextDisabled("ドラッグで動かす（Ctrl で 5° ごと）");
                }
            }
            ImGui::EndTooltip();
        }
    }
}

#endif // CORE_EDITOR
