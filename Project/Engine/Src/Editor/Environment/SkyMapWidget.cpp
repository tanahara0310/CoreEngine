#include "pch.h"
#include "Editor/Environment/SkyMapWidget.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Math/MathCore.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        namespace Theme = Editor::Theme;
        using View = SkyMapWidget::View;

        constexpr float kDegToRad = MathCore::Constants::kDegToRad;
        constexpr float kRadToDeg = MathCore::Constants::kRadToDeg;

        /// 図の高度の下限と、上限（天頂）までの幅 [deg]
        constexpr float kMinElevation = SunMoonController::kMinElevationDeg;
        constexpr float kElevationSpan = 90.0f - kMinElevation;

        /// 天球図の一辺の上限 [px] と、外周の方角の文字に空ける幅 [px]
        constexpr float kDomeMaxSize = 300.0f;
        constexpr float kDomeLabelMargin = 18.0f;

        /// 展開図の高さ（幅に掛ける割合と上下限 [px]）
        constexpr float kPanoramaAspect = 0.36f;
        constexpr float kPanoramaMinHeight = 120.0f;
        constexpr float kPanoramaMaxHeight = 220.0f;

        /// 印を掴める半径 [px]
        constexpr float kGrabRadius = 14.0f;

        /// Ctrl を押したときに止まる角度 [deg]
        constexpr float kSnapDeg = 5.0f;

        /// 太陽の軌道を分ける数（1 日を 15 分ごと）
        constexpr int kPathSamples = 96;

        /// カメラの見ている範囲の 1 辺を分ける数
        constexpr int kFrustumEdgeSamples = 8;

        /// 小さな文字の大きさの倍率
        constexpr float kSmallTextScale = 0.85f;

        /// @brief sRGB の 0〜255 の色
        struct Srgb
        {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
        };

        Srgb Mix(const Srgb& a, const Srgb& b, float t)
        {
            return Srgb{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
        }

        ImU32 ToU32(const Srgb& color, float alpha = 1.0f)
        {
            const auto channel = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 255.0f) + 0.5f); };
            return ImGui::GetColorU32(Theme::FromSrgb(channel(color.r), channel(color.g), channel(color.b), alpha));
        }

        ImU32 ToU32(const ImVec4& color, float alpha)
        {
            return ImGui::GetColorU32(Theme::WithAlpha(color, alpha));
        }

        float SmoothStep(float edge0, float edge1, float x)
        {
            const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        /// @brief 空の図の色（天頂・地平線・地面）
        struct SkyPalette
        {
            Srgb zenith;
            Srgb horizon;
            Srgb ground;
        };

        /// @brief 太陽の高さに合わせた空の図の色（夜・薄明・昼を混ぜる）
        SkyPalette PaletteFor(float sunElevationDeg)
        {
            constexpr SkyPalette kNight{ { 14, 18, 34 }, { 30, 38, 64 }, { 20, 20, 24 } };
            constexpr SkyPalette kTwilight{ { 44, 56, 104 }, { 190, 120, 88 }, { 40, 35, 38 } };
            constexpr SkyPalette kDay{ { 50, 102, 170 }, { 148, 182, 218 }, { 54, 50, 46 } };
            const float dusk = SmoothStep(-12.0f, -1.0f, sunElevationDeg);
            const float day = SmoothStep(-1.0f, 12.0f, sunElevationDeg);
            const auto blend = [&](const Srgb SkyPalette::* channel) {
                return Mix(Mix(kNight.*channel, kTwilight.*channel, dusk), kDay.*channel, day);
            };
            return SkyPalette{ blend(&SkyPalette::zenith), blend(&SkyPalette::horizon), blend(&SkyPalette::ground) };
        }

        // ===== 色（sRGB） =====
        constexpr Srgb kSunCore{ 255, 198, 72 };
        constexpr Srgb kSunLow{ 150, 108, 60 };
        constexpr Srgb kSunGlow{ 255, 190, 80 };
        constexpr Srgb kMoonCore{ 222, 230, 248 };
        constexpr Srgb kMoonLow{ 112, 120, 140 };
        constexpr Srgb kMoonGlow{ 180, 200, 240 };
        constexpr Srgb kMoonCrater{ 168, 178, 202 };
        constexpr Srgb kHandleOutline{ 24, 22, 30 };
        constexpr Srgb kPathColor{ 255, 214, 150 };
        constexpr Srgb kGridColor{ 236, 240, 248 };

        /// @brief 小さな文字を描く（中心か左端を合わせる）
        void SmallText(ImDrawList* drawList, ImVec2 position, ImU32 color, const char* text, float alignX = 0.0f)
        {
            const float size = ImGui::GetFontSize() * kSmallTextScale;
            const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
            position.x -= extent.x * alignX;
            position.y -= extent.y * 0.5f;
            drawList->AddText(ImGui::GetFont(), size, ImVec2(position.x + 1.0f, position.y + 1.0f),
                IM_COL32(0, 0, 0, 150), text);
            drawList->AddText(ImGui::GetFont(), size, position, color, text);
        }

        /// @brief 空の上の位置と図の上の位置を行き来する
        struct MapLayout
        {
            View view = View::Panorama;
            ImVec2 min{};
            ImVec2 max{};
            ImVec2 center{};    ///< 天球図の中心（天頂）
            float radius = 0.0f; ///< 天球図の外周（高度の下限）の半径

            float Width() const { return max.x - min.x; }
            float Height() const { return max.y - min.y; }

            /// @brief 天球図で、その高度の中心からの距離
            float DomeRadius(float elevationDeg) const
            {
                return (90.0f - std::clamp(elevationDeg, kMinElevation, 90.0f)) / kElevationSpan * radius;
            }

            /// @brief 展開図で、その高度の y
            float PanoramaY(float elevationDeg) const
            {
                return min.y + (90.0f - std::clamp(elevationDeg, kMinElevation, 90.0f)) / kElevationSpan * Height();
            }

            /// @brief 展開図で、その方位の x（北が左端・東が 1/4・南が中央）
            float PanoramaX(float azimuthDeg) const
            {
                float a = std::fmod(azimuthDeg, 360.0f);
                if (a < 0.0f) {
                    a += 360.0f;
                }
                return min.x + a / 360.0f * Width();
            }

            ImVec2 ToScreen(const SkyAngles& angles) const
            {
                if (view == View::Dome) {
                    const float r = DomeRadius(angles.elevationDeg);
                    const float a = angles.azimuthDeg * kDegToRad;
                    return ImVec2(center.x + r * std::sin(a), center.y - r * std::cos(a));
                }
                return ImVec2(PanoramaX(angles.azimuthDeg), PanoramaY(angles.elevationDeg));
            }

            SkyAngles FromScreen(const ImVec2& point) const
            {
                SkyAngles angles;
                if (view == View::Dome) {
                    const float dx = point.x - center.x;
                    const float dy = point.y - center.y;
                    const float r = std::sqrt(dx * dx + dy * dy);
                    angles.azimuthDeg = std::atan2(dx, -dy) * kRadToDeg;
                    angles.elevationDeg = 90.0f - r / (std::max)(radius, 1.0f) * kElevationSpan;
                } else {
                    const float u = std::clamp((point.x - min.x) / (std::max)(Width(), 1.0f), 0.0f, 1.0f);
                    const float v = std::clamp((point.y - min.y) / (std::max)(Height(), 1.0f), 0.0f, 1.0f);
                    angles.azimuthDeg = u * 360.0f;
                    angles.elevationDeg = 90.0f - v * kElevationSpan;
                }
                angles.elevationDeg = std::clamp(angles.elevationDeg, kMinElevation, 90.0f);
                angles.azimuthDeg = WrapAzimuth(angles.azimuthDeg);
                return angles;
            }
        };

        /// @brief 展開図で方位の継ぎ目をまたいで描くときの、横のずらし
        std::vector<float> WrapOffsets(const MapLayout& layout)
        {
            if (layout.view == View::Dome) {
                return { 0.0f };
            }
            return { -layout.Width(), 0.0f, layout.Width() };
        }

        /// @brief 空の上の点を図の上の点にする（展開図は前の点から方位が続くように x をずらす）
        /// @param clipBelow 高度の下限より下の点で線を切るか（切らなければ下限へ寄せる）
        std::vector<std::vector<ImVec2>> ToScreenRuns(const MapLayout& layout, const std::vector<SkyAngles>& points,
            bool clipBelow)
        {
            std::vector<std::vector<ImVec2>> runs(1);
            const float width = layout.Width();
            for (const SkyAngles& point : points) {
                if (clipBelow && point.elevationDeg < kMinElevation) {
                    if (!runs.back().empty()) {
                        runs.emplace_back();
                    }
                    continue;
                }
                ImVec2 screen = layout.ToScreen(point);
                std::vector<ImVec2>& run = runs.back();
                if (layout.view == View::Panorama && !run.empty()) {
                    const float previous = run.back().x;
                    while (screen.x - previous > width * 0.5f) { screen.x -= width; }
                    while (previous - screen.x > width * 0.5f) { screen.x += width; }
                }
                run.push_back(screen);
            }
            return runs;
        }

        /// @brief 空の上の点を順に結ぶ
        void DrawSkyPath(ImDrawList* drawList, const MapLayout& layout, const std::vector<SkyAngles>& points,
            ImU32 color, float thickness, bool clipBelow)
        {
            for (const std::vector<ImVec2>& run : ToScreenRuns(layout, points, clipBelow)) {
                if (run.size() < 2) {
                    continue;
                }
                for (const float offset : WrapOffsets(layout)) {
                    std::vector<ImVec2> shifted(run);
                    for (ImVec2& p : shifted) {
                        p.x += offset;
                    }
                    drawList->AddPolyline(shifted.data(), static_cast<int>(shifted.size()), color, ImDrawFlags_None, thickness);
                }
            }
        }

        // ==================== 背景と目盛り ====================

        void DrawDomeBackground(ImDrawList* drawList, const MapLayout& layout, const SkyPalette& palette)
        {
            const float horizon = layout.DomeRadius(0.0f);
            drawList->AddCircleFilled(layout.center, layout.radius, ToU32(palette.ground), 96);

            // 地平線から天頂へ、外の輪から順に重ねて塗る
            constexpr int kRings = 24;
            for (int i = kRings; i >= 1; --i) {
                const float t = static_cast<float>(i) / kRings;
                drawList->AddCircleFilled(layout.center, horizon * t,
                    ToU32(Mix(palette.zenith, palette.horizon, std::pow(t, 1.6f))), 96);
            }

            const ImU32 grid = ToU32(kGridColor, 0.16f);
            for (const float elevation : { 30.0f, 60.0f }) {
                drawList->AddCircle(layout.center, layout.DomeRadius(elevation), grid, 96, 1.0f);
            }
            for (int i = 0; i < 8; ++i) {
                const float a = static_cast<float>(i) * 45.0f * kDegToRad;
                drawList->AddLine(layout.center,
                    ImVec2(layout.center.x + layout.radius * std::sin(a), layout.center.y - layout.radius * std::cos(a)),
                    ToU32(kGridColor, 0.10f), 1.0f);
            }
            drawList->AddCircle(layout.center, horizon, ToU32(kGridColor, 0.85f), 96, 1.6f);
            drawList->AddCircle(layout.center, layout.radius, ToU32(kGridColor, 0.25f), 96, 1.0f);

            // 方角（外周の外）と高度（北東の線の上）
            static constexpr const char* kCardinals[] = { "北", "東", "南", "西" };
            for (int i = 0; i < 4; ++i) {
                const float a = static_cast<float>(i) * 90.0f * kDegToRad;
                const float r = layout.radius + kDomeLabelMargin * 0.5f;
                SmallText(drawList, ImVec2(layout.center.x + r * std::sin(a), layout.center.y - r * std::cos(a)),
                    ImGui::GetColorU32(i == 0 ? Theme::kText : Theme::kTextDim), kCardinals[i], 0.5f);
            }
            for (const float elevation : { 0.0f, 30.0f, 60.0f }) {
                char text[16];
                std::snprintf(text, sizeof(text), "%.0f°", elevation);
                const ImVec2 at = layout.ToScreen(SkyAngles{ elevation, 22.5f });
                SmallText(drawList, at, ToU32(kGridColor, 0.55f), text, 0.5f);
            }
        }

        void DrawPanoramaBackground(ImDrawList* drawList, const MapLayout& layout, const SkyPalette& palette)
        {
            const float horizonY = layout.PanoramaY(0.0f);
            const ImU32 zenith = ToU32(palette.zenith);
            const ImU32 horizon = ToU32(palette.horizon);
            drawList->AddRectFilledMultiColor(layout.min, ImVec2(layout.max.x, horizonY), zenith, zenith, horizon, horizon);
            const ImU32 groundTop = ToU32(palette.ground);
            const ImU32 groundBottom = ToU32(Mix(palette.ground, Srgb{ 0, 0, 0 }, 0.45f));
            drawList->AddRectFilledMultiColor(ImVec2(layout.min.x, horizonY), layout.max,
                groundTop, groundTop, groundBottom, groundBottom);

            const ImU32 grid = ToU32(kGridColor, 0.16f);
            for (const float elevation : { 30.0f, 60.0f }) {
                const float y = layout.PanoramaY(elevation);
                drawList->AddLine(ImVec2(layout.min.x, y), ImVec2(layout.max.x, y), grid, 1.0f);
                char text[16];
                std::snprintf(text, sizeof(text), "%.0f°", elevation);
                SmallText(drawList, ImVec2(layout.min.x + 4.0f, y - 7.0f), ToU32(kGridColor, 0.55f), text);
            }

            static constexpr const char* kNames[] = { "北", "北東", "東", "南東", "南", "南西", "西", "北西", "北" };
            const float labelY = horizonY + (layout.max.y - horizonY) * 0.5f;
            for (int i = 0; i <= 8; ++i) {
                const float x = layout.min.x + layout.Width() * static_cast<float>(i) / 8.0f;
                if (i > 0 && i < 8) {
                    drawList->AddLine(ImVec2(x, layout.min.y), ImVec2(x, layout.max.y), ToU32(kGridColor, 0.10f), 1.0f);
                }
                const bool cardinal = (i % 2) == 0;
                const float align = i == 0 ? 0.0f : (i == 8 ? 1.0f : 0.5f);
                const float inset = i == 0 ? 4.0f : (i == 8 ? -4.0f : 0.0f);
                SmallText(drawList, ImVec2(x + inset, labelY),
                    ImGui::GetColorU32(cardinal ? Theme::kTextDim : Theme::kTextMute), kNames[i], align);
            }
            drawList->AddLine(ImVec2(layout.min.x, horizonY), ImVec2(layout.max.x, horizonY), ToU32(kGridColor, 0.85f), 1.6f);
            SmallText(drawList, ImVec2(layout.max.x - 4.0f, horizonY - 8.0f), ToU32(kGridColor, 0.55f), "地平線", 1.0f);
        }

        // ==================== 太陽の軌道とカメラ ====================

        void DrawSunPath(ImDrawList* drawList, const MapLayout& layout, const SunMoonController& controller)
        {
            std::vector<SkyAngles> path(kPathSamples + 1);
            for (int i = 0; i <= kPathSamples; ++i) {
                path[i] = controller.SunAnglesAt(24.0f * static_cast<float>(i) / kPathSamples);
            }
            DrawSkyPath(drawList, layout, path, ToU32(kPathColor, 0.45f), 1.4f, true);

            for (int hour = 0; hour < 24; hour += 3) {
                const SkyAngles angles = controller.SunAnglesAt(static_cast<float>(hour));
                if (angles.elevationDeg < kMinElevation) {
                    continue;
                }
                const ImVec2 at = layout.ToScreen(angles);
                drawList->AddCircleFilled(at, 2.2f, ToU32(kPathColor, 0.7f), 12);
                if (hour % 6 == 0 && hour != 0) {
                    char text[16];
                    std::snprintf(text, sizeof(text), "%d時", hour);
                    SmallText(drawList, ImVec2(at.x + 5.0f, at.y - 8.0f), ToU32(kPathColor, 0.85f), text);
                }
            }
        }

        void DrawCameraView(ImDrawList* drawList, const MapLayout& layout, const SkyViewCamera& camera)
        {
            const SkyAngles centerAngles = DirectionToSkyAngles(camera.forward, 0.0f);
            const auto directionAt = [&](float sx, float sy) {
                return camera.forward + camera.right * (sx * camera.tanHalfFovX) + camera.up * (sy * camera.tanHalfFovY);
            };

            // 見ている範囲の外周（上 → 右 → 下 → 左）
            std::vector<SkyAngles> outline;
            outline.reserve(kFrustumEdgeSamples * 4 + 1);
            for (int edge = 0; edge < 4; ++edge) {
                for (int i = 0; i < kFrustumEdgeSamples; ++i) {
                    const float s = -1.0f + 2.0f * static_cast<float>(i) / kFrustumEdgeSamples;
                    const Vector3 direction = edge == 0 ? directionAt(s, 1.0f)
                        : edge == 1 ? directionAt(1.0f, -s)
                        : edge == 2 ? directionAt(-s, -1.0f)
                        : directionAt(-1.0f, s);
                    SkyAngles angles = DirectionToSkyAngles(direction, centerAngles.azimuthDeg);
                    angles.elevationDeg = (std::max)(angles.elevationDeg, kMinElevation);
                    outline.push_back(angles);
                }
            }
            outline.push_back(outline.front());

            std::vector<std::vector<ImVec2>> runs = ToScreenRuns(layout, outline, false);
            if (runs.empty() || runs.front().size() < 3) {
                return;
            }
            const std::vector<ImVec2>& points = runs.front();
            ImVec2 middle = layout.ToScreen(SkyAngles{ (std::max)(centerAngles.elevationDeg, kMinElevation), centerAngles.azimuthDeg });
            if (layout.view == View::Panorama) {
                const float width = layout.Width();
                while (middle.x - points.front().x > width * 0.5f) { middle.x -= width; }
                while (points.front().x - middle.x > width * 0.5f) { middle.x += width; }
            }

            // 塗りの縁のぼかしを切って、三角形を並べて塗る
            const ImDrawListFlags savedFlags = drawList->Flags;
            drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            const ImU32 fill = ToU32(Theme::kAccentHover, 0.16f);
            for (const float offset : WrapOffsets(layout)) {
                const ImVec2 m(middle.x + offset, middle.y);
                for (size_t i = 0; i + 1 < points.size(); ++i) {
                    drawList->AddTriangleFilled(m, ImVec2(points[i].x + offset, points[i].y),
                        ImVec2(points[i + 1].x + offset, points[i + 1].y), fill);
                }
            }
            drawList->Flags = savedFlags;

            const ImU32 line = ToU32(Theme::kAccentHover, 0.85f);
            for (const float offset : WrapOffsets(layout)) {
                std::vector<ImVec2> shifted(points);
                for (ImVec2& p : shifted) {
                    p.x += offset;
                }
                drawList->AddPolyline(shifted.data(), static_cast<int>(shifted.size()), line, ImDrawFlags_None, 1.3f);
                SmallText(drawList, ImVec2(middle.x + offset, middle.y), line, "カメラ", 0.5f);
            }
        }

        // ==================== 印 ====================

        void DrawSunHandle(ImDrawList* drawList, const ImVec2& at, bool aboveHorizon, bool highlighted)
        {
            drawList->AddCircleFilled(at, 12.0f, ToU32(kSunGlow, aboveHorizon ? 0.28f : 0.12f), 24);
            const Srgb core = aboveHorizon ? kSunCore : kSunLow;
            if (aboveHorizon) {
                for (int i = 0; i < 8; ++i) {
                    const float a = static_cast<float>(i) * 45.0f * kDegToRad;
                    const ImVec2 dir(std::cos(a), std::sin(a));
                    drawList->AddLine(ImVec2(at.x + dir.x * 9.0f, at.y + dir.y * 9.0f),
                        ImVec2(at.x + dir.x * 12.5f, at.y + dir.y * 12.5f), ToU32(core), 1.5f);
                }
            }
            drawList->AddCircleFilled(at, 6.5f, ToU32(core), 20);
            drawList->AddCircle(at, 6.5f, ToU32(kHandleOutline, 0.8f), 20, 1.2f);
            if (highlighted) {
                drawList->AddCircle(at, 15.0f, ImGui::GetColorU32(Theme::kText), 32, 1.5f);
            }
        }

        void DrawMoonHandle(ImDrawList* drawList, const ImVec2& at, bool aboveHorizon, bool highlighted)
        {
            drawList->AddCircleFilled(at, 10.0f, ToU32(kMoonGlow, aboveHorizon ? 0.22f : 0.10f), 24);
            drawList->AddCircleFilled(at, 5.8f, ToU32(aboveHorizon ? kMoonCore : kMoonLow), 20);
            drawList->AddCircleFilled(ImVec2(at.x - 1.6f, at.y - 1.2f), 1.3f, ToU32(kMoonCrater), 10);
            drawList->AddCircleFilled(ImVec2(at.x + 1.9f, at.y + 1.6f), 1.0f, ToU32(kMoonCrater), 10);
            drawList->AddCircle(at, 5.8f, ToU32(kHandleOutline, 0.8f), 20, 1.2f);
            if (highlighted) {
                drawList->AddCircle(at, 13.0f, ImGui::GetColorU32(Theme::kText), 32, 1.5f);
            }
        }

        /// @brief 天体の印の図の上の位置（展開図で端に近いときは反対の端にも出す）
        std::vector<ImVec2> HandlePositions(const MapLayout& layout, const SkyAngles& angles)
        {
            const ImVec2 at = layout.ToScreen(angles);
            std::vector<ImVec2> positions{ at };
            if (layout.view == View::Panorama) {
                if (at.x - layout.min.x < kGrabRadius) {
                    positions.emplace_back(at.x + layout.Width(), at.y);
                } else if (layout.max.x - at.x < kGrabRadius) {
                    positions.emplace_back(at.x - layout.Width(), at.y);
                }
            }
            return positions;
        }

        float DistanceSquared(const ImVec2& a, const ImVec2& b)
        {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            return dx * dx + dy * dy;
        }

        const char* BodyName(SkyBody body)
        {
            return body == SkyBody::Sun ? "太陽" : "月";
        }
    }

    void SkyMapWidget::Draw(SunMoonController& controller, View view)
    {
        ImGui::PushID("SkyMap");

        const float available = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
        MapLayout layout;
        layout.view = view;
        ImVec2 size;
        if (view == View::Dome) {
            const float side = (std::min)(available, kDomeMaxSize);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - side) * 0.5f);
            size = ImVec2(side, side);
        } else {
            size = ImVec2(available, std::clamp(available * kPanoramaAspect, kPanoramaMinHeight, kPanoramaMaxHeight));
        }
        layout.min = ImGui::GetCursorScreenPos();
        layout.max = ImVec2(layout.min.x + size.x, layout.min.y + size.y);
        layout.center = ImVec2(layout.min.x + size.x * 0.5f, layout.min.y + size.y * 0.5f);
        layout.radius = (std::max)(1.0f, size.x * 0.5f - kDomeLabelMargin);

        ImGui::InvisibleButton("##map", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        controller.TrackItem();
        const bool hovered = ImGui::IsItemHovered();
        const bool activated = ImGui::IsItemActivated();
        const bool active = ImGui::IsItemActive();
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 mouse = io.MousePos;

        // カーソルにいちばん近い印（掴める半径の中だけ）
        const auto nearestHandle = [&]() -> std::optional<SkyBody> {
            std::optional<SkyBody> nearest;
            float best = kGrabRadius * kGrabRadius;
            for (const SkyBody body : { SkyBody::Sun, SkyBody::Moon }) {
                if (!controller.IsShown(body)) {
                    continue;
                }
                for (const ImVec2& at : HandlePositions(layout, controller.GetAngles(body))) {
                    const float distance = DistanceSquared(mouse, at);
                    if (distance <= best) {
                        best = distance;
                        nearest = body;
                    }
                }
            }
            return nearest;
        };

        // 掴んだ物を決める（右は月・左は掴んだ印か太陽）
        if (activated) {
            SkyBody body = SkyBody::Sun;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                body = SkyBody::Moon;
            } else if (const std::optional<SkyBody> grabbed = nearestHandle()) {
                body = *grabbed;
            }
            dragging_ = controller.CanMove(body) ? std::optional<SkyBody>(body) : std::nullopt;
        }
        if (!active) {
            dragging_.reset();
        }
        if (active && dragging_) {
            SkyAngles angles = layout.FromScreen(mouse);
            if (io.KeyCtrl) {
                angles.elevationDeg = std::round(angles.elevationDeg / kSnapDeg) * kSnapDeg;
                angles.azimuthDeg = WrapAzimuth(std::round(angles.azimuthDeg / kSnapDeg) * kSnapDeg);
            }
            controller.MoveTo(*dragging_, angles);
        }

        const std::optional<SkyBody> highlighted = dragging_ ? dragging_ : (hovered ? nearestHandle() : std::nullopt);

        // ===== 描く =====
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(layout.min, layout.max, true);

        const SkyPalette palette = PaletteFor(controller.GetSun().angles.elevationDeg);
        if (view == View::Dome) {
            DrawDomeBackground(drawList, layout, palette);
        } else {
            DrawPanoramaBackground(drawList, layout, palette);
        }
        if (const SkyViewCamera* const camera = controller.GetViewCamera()) {
            DrawCameraView(drawList, layout, *camera);
        }
        DrawSunPath(drawList, layout, controller);

        // 月 → 太陽の順（重なったら太陽が上）
        if (controller.IsShown(SkyBody::Moon)) {
            const SkyAngles angles = controller.GetAngles(SkyBody::Moon);
            for (const ImVec2& at : HandlePositions(layout, angles)) {
                DrawMoonHandle(drawList, at, angles.elevationDeg >= 0.0f, highlighted == SkyBody::Moon);
            }
        }
        if (controller.IsShown(SkyBody::Sun)) {
            const SkyAngles angles = controller.GetAngles(SkyBody::Sun);
            for (const ImVec2& at : HandlePositions(layout, angles)) {
                DrawSunHandle(drawList, at, angles.elevationDeg >= 0.0f, highlighted == SkyBody::Sun);
            }
        }

        // カーソルの位置の読み（印に乗っていないとき）
        if (hovered && !highlighted) {
            const SkyAngles at = layout.FromScreen(mouse);
            char text[64];
            std::snprintf(text, sizeof(text), "高度 %.0f°  方位 %.0f°（%s）", at.elevationDeg, AzimuthForDisplay(at.azimuthDeg),
                CompassName(at.azimuthDeg));
            const float y = view == View::Dome ? layout.max.y - 8.0f : layout.min.y + 10.0f;
            const float x = view == View::Dome ? layout.min.x + 2.0f : layout.max.x - 4.0f;
            SmallText(drawList, ImVec2(x, y), ImGui::GetColorU32(Theme::kText), text, view == View::Dome ? 0.0f : 1.0f);
        }

        drawList->PopClipRect();

        // ===== 説明 =====
        if (highlighted) {
            const SkyBody body = *highlighted;
            const SkyAngles angles = controller.GetAngles(body);
            if (!dragging_) {
                ImGui::SetMouseCursor(controller.CanMove(body) ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_NotAllowed);
            }
            ImGui::BeginTooltip();
            ImGui::Text("%s　高度 %.1f°　方位 %.1f°（%s）", BodyName(body), angles.elevationDeg, AzimuthForDisplay(angles.azimuthDeg),
                CompassName(angles.azimuthDeg));
            if (!dragging_) {
                if (controller.IsTimeDriven()) {
                    ImGui::TextDisabled(body == SkyBody::Sun
                        ? "ドラッグで軌道に沿って時刻を動かす" : "ドラッグで軌道に沿って月齢を動かす");
                } else {
                    ImGui::TextDisabled("ドラッグで動かす（Ctrl で 5° ごと）");
                }
            }
            ImGui::EndTooltip();
        }

        ImGui::PopID();
    }
}

#endif // CORE_EDITOR
