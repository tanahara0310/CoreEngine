#include "pch.h"
#include "Editor/Scene/SceneViewTools.h"

#ifdef CORE_EDITOR

#include "Camera/Camera.h"
#include "Math/MathCore.h"
#include "Math/Vector/Vector4.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace CoreEngine::Editor
{
    namespace
    {
        /// これより手前（カメラからの奥行き）は描かない
        constexpr float kNearDepth = 1.0e-3f;

        /// 円を分ける数
        constexpr int kCircleSegments = 48;

        Vector3 Add(const Vector3& a, const Vector3& b) { return Vector3(a.x + b.x, a.y + b.y, a.z + b.z); }
        Vector3 Sub(const Vector3& a, const Vector3& b) { return Vector3(a.x - b.x, a.y - b.y, a.z - b.z); }
        Vector3 Scale(const Vector3& v, float s) { return Vector3(v.x * s, v.y * s, v.z * s); }
        float Dot3(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        Vector3 Normalize(const Vector3& v, const Vector3& fallback)
        {
            const float length = std::sqrt(Dot3(v, v));
            return length > 1.0e-6f ? Scale(v, 1.0f / length) : fallback;
        }

        /// @brief normal に垂直な 2 本の軸
        void Basis(const Vector3& normal, Vector3& u, Vector3& v)
        {
            const Vector3 n = Normalize(normal, Vector3(0.0f, 1.0f, 0.0f));
            const Vector3 helper = std::abs(n.y) < 0.99f ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
            u = Normalize(Cross(n, helper), Vector3(1.0f, 0.0f, 0.0f));
            v = Cross(n, u);
        }

        Vector3 CirclePoint(const Vector3& center, const Vector3& u, const Vector3& v, float radius, int index)
        {
            const float angle = static_cast<float>(index) * 2.0f * std::numbers::pi_v<float> / kCircleSegments;
            return Add(center, Add(Scale(u, std::cos(angle) * radius), Scale(v, std::sin(angle) * radius)));
        }

        const SceneViewContext* Context()
        {
            return SceneViewTools::Get().Current();
        }

        Vector4 ToClip(const SceneViewContext& context, const Vector3& world)
        {
            return MathCore::CoordinateTransform::TransformCoord(Vector4{ world.x, world.y, world.z, 1.0f },
                context.viewProjection);
        }

        ImVec2 ClipToScreen(const SceneViewContext& context, const Vector4& clip)
        {
            const float ndcX = clip.x / clip.w;
            const float ndcY = clip.y / clip.w;
            return ImVec2(context.position.x + (ndcX * 0.5f + 0.5f) * context.size.x,
                context.position.y + (0.5f - ndcY * 0.5f) * context.size.y);
        }

        Vector4 LerpClip(const Vector4& a, const Vector4& b, float t)
        {
            return Vector4{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
        }

        /// @brief 線分をカメラの手前で切って画面の座標にする
        /// @return 全部カメラの後ろなら false
        bool ProjectSegment(const SceneViewContext& context, const Vector3& from, const Vector3& to, ImVec2& a, ImVec2& b)
        {
            Vector4 clipA = ToClip(context, from);
            Vector4 clipB = ToClip(context, to);
            const bool frontA = clipA.w > kNearDepth;
            const bool frontB = clipB.w > kNearDepth;
            if (!frontA && !frontB) {
                return false;
            }
            if (!frontA) {
                clipA = LerpClip(clipA, clipB, (kNearDepth - clipA.w) / (clipB.w - clipA.w));
            } else if (!frontB) {
                clipB = LerpClip(clipA, clipB, (kNearDepth - clipA.w) / (clipB.w - clipA.w));
            }
            a = ClipToScreen(context, clipA);
            b = ClipToScreen(context, clipB);
            return true;
        }

        ImDrawList* DrawList()
        {
            return ImGui::GetWindowDrawList();
        }
    }

    SceneViewTools& SceneViewTools::Get()
    {
        static SceneViewTools instance;
        return instance;
    }

    ScopedRegistration SceneViewTools::Register(std::function<void(const SceneViewContext&)> draw)
    {
        const std::uint64_t id = ++lastId_;
        entries_.push_back(Entry{ id, std::move(draw) });
        return ScopedRegistration([this, id] {
            std::erase_if(entries_, [id](const Entry& entry) { return entry.id == id; });
        });
    }

    void SceneViewTools::Draw(const SceneViewContext& context)
    {
        requested_ = false;
        if (!context.camera || context.size.x <= 0.0f || context.size.y <= 0.0f) {
            wantsMouse_ = false;
            return;
        }

        SceneViewContext frame = context;
        frame.viewProjection = context.camera->GetViewMatrix() * context.camera->GetProjectionMatrix();
        current_ = &frame;

        ImDrawList* const drawList = DrawList();
        drawList->PushClipRect(frame.position, ImVec2(frame.position.x + frame.size.x, frame.position.y + frame.size.y), true);

        // 描いている間に登録が増減しても回り切れるよう、写しを回す
        const std::vector<Entry> entries = entries_;
        for (const Entry& entry : entries) {
            if (entry.draw) {
                entry.draw(frame);
            }
        }

        drawList->PopClipRect();
        current_ = nullptr;
        wantsMouse_ = requested_;
    }

    namespace SceneOverlay
    {
        bool WorldToScreen(const Vector3& world, ImVec2& screen)
        {
            const SceneViewContext* const context = Context();
            if (!context) {
                return false;
            }
            const Vector4 clip = ToClip(*context, world);
            if (clip.w <= kNearDepth) {
                return false;
            }
            screen = ClipToScreen(*context, clip);
            return true;
        }

        void Line(const Vector3& from, const Vector3& to, ImU32 color, float thickness)
        {
            const SceneViewContext* const context = Context();
            ImVec2 a;
            ImVec2 b;
            if (context && ProjectSegment(*context, from, to, a, b)) {
                DrawList()->AddLine(a, b, color, thickness);
            }
        }

        void Polyline(const Vector3* points, std::size_t count, bool closed, ImU32 color, float thickness)
        {
            if (!points || count < 2) {
                return;
            }
            for (std::size_t i = 0; i + 1 < count; ++i) {
                Line(points[i], points[i + 1], color, thickness);
            }
            if (closed && count > 2) {
                Line(points[count - 1], points[0], color, thickness);
            }
        }

        void WireBox(const Vector3& center, const Vector3& size, ImU32 color, float thickness)
        {
            const Vector3 h(size.x * 0.5f, size.y * 0.5f, size.z * 0.5f);
            std::array<Vector3, 8> corners;
            for (int i = 0; i < 8; ++i) {
                corners[i] = Vector3(center.x + ((i & 1) ? h.x : -h.x), center.y + ((i & 2) ? h.y : -h.y),
                    center.z + ((i & 4) ? h.z : -h.z));
            }
            static constexpr int kEdges[12][2] = {
                { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
                { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
                { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
            };
            for (const auto& edge : kEdges) {
                Line(corners[edge[0]], corners[edge[1]], color, thickness);
            }
        }

        void WireDisc(const Vector3& center, const Vector3& normal, float radius, ImU32 color, float thickness)
        {
            Vector3 u;
            Vector3 v;
            Basis(normal, u, v);
            Vector3 previous = CirclePoint(center, u, v, radius, 0);
            for (int i = 1; i <= kCircleSegments; ++i) {
                const Vector3 current = CirclePoint(center, u, v, radius, i);
                Line(previous, current, color, thickness);
                previous = current;
            }
        }

        void WireSphere(const Vector3& center, float radius, ImU32 color, float thickness)
        {
            WireDisc(center, Vector3(1.0f, 0.0f, 0.0f), radius, color, thickness);
            WireDisc(center, Vector3(0.0f, 1.0f, 0.0f), radius, color, thickness);
            WireDisc(center, Vector3(0.0f, 0.0f, 1.0f), radius, color, thickness);

            // カメラから見た球の輪郭（接する円は、中心からカメラへ r²/d 寄った所にある）
            const SceneViewContext* const context = Context();
            if (!context || !context->camera) {
                return;
            }
            const Vector3 toCamera = Sub(context->camera->GetPosition(), center);
            const float distance = std::sqrt(Dot3(toCamera, toCamera));
            if (distance <= radius) {
                return;
            }
            const Vector3 n = Scale(toCamera, 1.0f / distance);
            const float ratio = radius / distance;
            WireDisc(Add(center, Scale(n, radius * ratio)), n, radius * std::sqrt(1.0f - ratio * ratio), color, thickness);
        }

        void SolidDisc(const Vector3& center, const Vector3& normal, float radius, ImU32 color)
        {
            Vector3 u;
            Vector3 v;
            Basis(normal, u, v);
            std::array<ImVec2, kCircleSegments> points;
            for (int i = 0; i < kCircleSegments; ++i) {
                if (!WorldToScreen(CirclePoint(center, u, v, radius, i), points[i])) {
                    return;
                }
            }
            DrawList()->AddConvexPolyFilled(points.data(), kCircleSegments, color);
        }

        void SolidQuad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, ImU32 color)
        {
            std::array<ImVec2, 4> points;
            const Vector3* const corners[] = { &a, &b, &c, &d };
            for (int i = 0; i < 4; ++i) {
                if (!WorldToScreen(*corners[i], points[i])) {
                    return;
                }
            }
            DrawList()->AddQuadFilled(points[0], points[1], points[2], points[3], color);
        }

        void Dot(const Vector3& position, float size, ImU32 color)
        {
            ImVec2 screen;
            if (WorldToScreen(position, screen)) {
                DrawList()->AddCircleFilled(screen, (std::max)(1.0f, size * 0.5f), color, 16);
            }
        }

        void Label(const Vector3& position, const std::string& text, ImU32 color)
        {
            ImVec2 screen;
            if (!WorldToScreen(position, screen) || text.empty()) {
                return;
            }
            const ImVec2 at(screen.x + 6.0f, screen.y + 4.0f);
            ImDrawList* const drawList = DrawList();
            drawList->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 200), text.c_str());
            drawList->AddText(at, color, text.c_str());
        }
    }
}

#endif // CORE_EDITOR
