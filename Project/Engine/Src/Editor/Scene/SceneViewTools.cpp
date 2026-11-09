#include "pch.h"
#include "Editor/Scene/SceneViewTools.h"

#ifdef CORE_EDITOR

#include "Camera/Camera.h"
#include "Editor/Scene/SceneOverlayOcclusion.h"
#include "Math/MathCore.h"
#include "Math/Vector/Vector4.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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

        ImDrawList* DrawList()
        {
            return ImGui::GetWindowDrawList();
        }

        /// @brief 画面の上の点と、その点の深度（0〜1）
        struct ScreenPoint
        {
            ImVec2 position{};
            float depth = 0.0f;
        };

        ScreenPoint ToScreenPoint(const SceneViewContext& context, const Vector4& clip)
        {
            return ScreenPoint{ ClipToScreen(context, clip), clip.z / clip.w };
        }

        /// @brief BeginOccluded の間か
        bool g_occluding = false;

        /// @brief 隠れた所の濃さ
        float g_hiddenAlpha = 1.0f;

        /// @brief BeginOccluded の前の描画リストのフラグ
        ImDrawListFlags g_savedFlags = ImDrawListFlags_None;

        /// @brief first から後に足した頂点の uv へ、いちばん近い点の深度と隠れた所の濃さを入れる
        void TagVertices(ImDrawList& drawList, int first, const ScreenPoint* points, std::size_t count)
        {
            if (!g_occluding || count == 0) {
                return;
            }
            for (int i = first; i < drawList.VtxBuffer.Size; ++i) {
                ImDrawVert& vertex = drawList.VtxBuffer[i];
                std::size_t nearest = 0;
                float nearestDistance = std::numeric_limits<float>::max();
                for (std::size_t p = 0; p < count; ++p) {
                    const float dx = vertex.pos.x - points[p].position.x;
                    const float dy = vertex.pos.y - points[p].position.y;
                    const float distance = dx * dx + dy * dy;
                    if (distance < nearestDistance) {
                        nearestDistance = distance;
                        nearest = p;
                    }
                }
                vertex.uv = ImVec2(points[nearest].depth, g_hiddenAlpha);
            }
        }

        /// @brief 画面の点を順に結んで描く
        void StrokeScreen(const std::vector<ScreenPoint>& points, bool closed, ImU32 color, float thickness)
        {
            if (points.size() < 2) {
                return;
            }
            std::vector<ImVec2> positions(points.size());
            for (std::size_t i = 0; i < points.size(); ++i) {
                positions[i] = points[i].position;
            }
            ImDrawList& drawList = *DrawList();
            const int first = drawList.VtxBuffer.Size;
            drawList.AddPolyline(positions.data(), static_cast<int>(positions.size()), color,
                closed ? ImDrawFlags_Closed : ImDrawFlags_None, thickness);
            TagVertices(drawList, first, points.data(), points.size());
        }

        /// @brief ワールドの点を順に結んで描く（カメラの後ろにかかる所は切る）
        void Stroke(const SceneViewContext& context, const Vector3* points, std::size_t count, bool closed,
            ImU32 color, float thickness)
        {
            if (!points || count < 2) {
                return;
            }
            std::vector<Vector4> clips(count);
            bool allFront = true;
            for (std::size_t i = 0; i < count; ++i) {
                clips[i] = ToClip(context, points[i]);
                allFront = allFront && clips[i].w > kNearDepth;
            }

            std::vector<ScreenPoint> run;
            run.reserve(count + 1);
            if (allFront) {
                for (const Vector4& clip : clips) {
                    run.push_back(ToScreenPoint(context, clip));
                }
                StrokeScreen(run, closed, color, thickness);
                return;
            }

            // カメラの手前にある続きごとに分けて描く
            const auto flush = [&] {
                StrokeScreen(run, false, color, thickness);
                run.clear();
            };
            const std::size_t segments = closed ? count : count - 1;
            for (std::size_t s = 0; s < segments; ++s) {
                const Vector4& a = clips[s];
                const Vector4& b = clips[(s + 1) % count];
                const bool frontA = a.w > kNearDepth;
                const bool frontB = b.w > kNearDepth;
                if (!frontA && !frontB) {
                    flush();
                    continue;
                }
                if (frontA && frontB) {
                    if (run.empty()) {
                        run.push_back(ToScreenPoint(context, a));
                    }
                    run.push_back(ToScreenPoint(context, b));
                    continue;
                }
                const Vector4 cut = LerpClip(a, b, (kNearDepth - a.w) / (b.w - a.w));
                if (frontA) {
                    if (run.empty()) {
                        run.push_back(ToScreenPoint(context, a));
                    }
                    run.push_back(ToScreenPoint(context, cut));
                    flush();
                } else {
                    flush();
                    run.push_back(ToScreenPoint(context, cut));
                    run.push_back(ToScreenPoint(context, b));
                }
            }
            flush();
        }

        /// @brief 塗りの角を画面の点にする
        /// @return どれかがカメラの後ろなら false
        template <std::size_t N>
        bool ProjectCorners(const SceneViewContext& context, const Vector3* const (&corners)[N], std::array<ScreenPoint, N>& points)
        {
            for (std::size_t i = 0; i < N; ++i) {
                const Vector4 clip = ToClip(context, *corners[i]);
                if (clip.w <= kNearDepth) {
                    return false;
                }
                points[i] = ToScreenPoint(context, clip);
            }
            return true;
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
                SceneOverlay::EndOccluded();
            }
        }

        drawList->PopClipRect();
        current_ = nullptr;
        wantsMouse_ = requested_;
    }

    namespace SceneOverlay
    {
        void BeginOccluded(float hiddenAlpha)
        {
            const SceneViewContext* const context = Context();
            if (g_occluding || !context || !context->camera) {
                return;
            }
            ImDrawList& drawList = *DrawList();
            if (!SceneOverlayOcclusion::Get().Begin(drawList, context->position, context->size, *context->camera)) {
                return;
            }
            g_occluding = true;
            g_hiddenAlpha = std::clamp(hiddenAlpha, 0.0f, 1.0f);
            // 線の縁のぼかしを頂点の色で作る（uv は深度に使う）
            g_savedFlags = drawList.Flags;
            drawList.Flags &= ~ImDrawListFlags_AntiAliasedLinesUseTex;
        }

        void EndOccluded()
        {
            if (!g_occluding) {
                return;
            }
            ImDrawList& drawList = *DrawList();
            drawList.Flags = g_savedFlags;
            SceneOverlayOcclusion::Get().End(drawList);
            g_occluding = false;
        }

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
            if (const SceneViewContext* const context = Context()) {
                const Vector3 points[] = { from, to };
                Stroke(*context, points, 2, false, color, thickness);
            }
        }

        void Polyline(const Vector3* points, std::size_t count, bool closed, ImU32 color, float thickness)
        {
            if (const SceneViewContext* const context = Context()) {
                Stroke(*context, points, count, closed && count > 2, color, thickness);
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
            std::array<Vector3, kCircleSegments> points;
            for (int i = 0; i < kCircleSegments; ++i) {
                points[i] = CirclePoint(center, u, v, radius, i);
            }
            Polyline(points.data(), points.size(), true, color, thickness);
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
            const SceneViewContext* const context = Context();
            if (!context) {
                return;
            }
            Vector3 u;
            Vector3 v;
            Basis(normal, u, v);
            std::array<ScreenPoint, kCircleSegments> points;
            std::array<ImVec2, kCircleSegments> positions;
            for (int i = 0; i < kCircleSegments; ++i) {
                const Vector4 clip = ToClip(*context, CirclePoint(center, u, v, radius, i));
                if (clip.w <= kNearDepth) {
                    return;
                }
                points[i] = ToScreenPoint(*context, clip);
                positions[i] = points[i].position;
            }
            ImDrawList& drawList = *DrawList();
            const int first = drawList.VtxBuffer.Size;
            drawList.AddConvexPolyFilled(positions.data(), kCircleSegments, color);
            TagVertices(drawList, first, points.data(), points.size());
        }

        void SolidQuad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, ImU32 color)
        {
            const SceneViewContext* const context = Context();
            const Vector3* const corners[] = { &a, &b, &c, &d };
            std::array<ScreenPoint, 4> points;
            if (!context || !ProjectCorners(*context, corners, points)) {
                return;
            }
            ImDrawList& drawList = *DrawList();
            const int first = drawList.VtxBuffer.Size;
            drawList.AddQuadFilled(points[0].position, points[1].position, points[2].position, points[3].position, color);
            TagVertices(drawList, first, points.data(), points.size());
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
            // 文字はフォントの絵を使うので、深度と比べる区間を一度閉じて描く
            const bool occluding = g_occluding;
            const float hiddenAlpha = g_hiddenAlpha;
            EndOccluded();
            const ImVec2 at(screen.x + 6.0f, screen.y + 4.0f);
            ImDrawList* const drawList = DrawList();
            drawList->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 200), text.c_str());
            drawList->AddText(at, color, text.c_str());
            if (occluding) {
                BeginOccluded(hiddenAlpha);
            }
        }
    }
}

#endif // CORE_EDITOR
