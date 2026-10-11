#include "pch.h"
#include "Editor/Script/EditorGUIState.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Math/Curve/KeyCurve.h"
#include "Math/Vector/Vector2.h"
#include "Script/Binding/BindingRegistrar.h"

#include <angelscript.h>
#include <imgui.h>
#include <scriptarray/scriptarray.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace CoreEngine::Editor::ScriptBinding
{
    namespace
    {
        /// 点の半径（ピクセル）
        constexpr float kKeyRadius = 5.0f;

        /// 点に当たったと見なす距離（ピクセル）
        constexpr float kHitRadius = 7.0f;

        /// カーブの線を分ける数
        constexpr int kCurveSamples = 96;

        /// 時間軸の目盛りの行と、トラック 1 本の高さ
        constexpr float kRulerHeight = 22.0f;
        constexpr float kTrackHeight = 24.0f;

        /// 時間軸の、トラックの名前の列と目盛りの間の空き
        constexpr float kTimelinePadding = 8.0f;

        ImU32 Color(const ImVec4& color)
        {
            return ImGui::GetColorU32(color);
        }

        void MarkChanged(bool changed)
        {
            if (changed) {
                FrameState().changed = true;
            }
        }

        std::vector<Vector2> ReadVector2s(const CScriptArray& array)
        {
            std::vector<Vector2> values;
            values.reserve(array.GetSize());
            for (asUINT i = 0; i < array.GetSize(); ++i) {
                values.push_back(*static_cast<const Vector2*>(array.At(i)));
            }
            return values;
        }

        void WriteVector2s(CScriptArray& array, const std::vector<Vector2>& values)
        {
            array.Resize(static_cast<asUINT>(values.size()));
            for (asUINT i = 0; i < array.GetSize(); ++i) {
                *static_cast<Vector2*>(array.At(i)) = values[i];
            }
        }

        std::vector<float> ReadFloats(const CScriptArray& array)
        {
            std::vector<float> values;
            values.reserve(array.GetSize());
            for (asUINT i = 0; i < array.GetSize(); ++i) {
                values.push_back(*static_cast<const float*>(array.At(i)));
            }
            return values;
        }

        void WriteFloats(CScriptArray& array, const std::vector<float>& values)
        {
            array.Resize(static_cast<asUINT>(values.size()));
            for (asUINT i = 0; i < array.GetSize(); ++i) {
                *static_cast<float*>(array.At(i)) = values[i];
            }
        }

        /// @brief 並べ直した後に、指していた要素の新しい番号を探す
        template<class T, class Less>
        int SortKeeping(std::vector<T>& values, int index, Less less)
        {
            if (index < 0 || index >= static_cast<int>(values.size())) {
                std::stable_sort(values.begin(), values.end(), less);
                return -1;
            }
            std::vector<int> order(values.size());
            for (std::size_t i = 0; i < order.size(); ++i) {
                order[i] = static_cast<int>(i);
            }
            std::stable_sort(order.begin(), order.end(), [&values, &less](int a, int b) { return less(values[a], values[b]); });
            std::vector<T> sorted;
            sorted.reserve(values.size());
            int moved = -1;
            for (std::size_t i = 0; i < order.size(); ++i) {
                sorted.push_back(values[order[i]]);
                if (order[i] == index) {
                    moved = static_cast<int>(i);
                }
            }
            values = std::move(sorted);
            return moved;
        }

        // ---------------------------------------------------------------- インスペクタ

        bool DrawDefaultInspector()
        {
            if (!RequireGUI("DrawDefaultInspector")) {
                return false;
            }
            const std::function<bool()>* const drawer = FrameState().drawDefaultInspector;
            if (!drawer) {
                ThrowScriptException("EditorGUI::DrawDefaultInspector は ComponentEditor の OnInspectorGUI の中でだけ使えます");
                return false;
            }
            const bool changed = (*drawer)();
            MarkChanged(changed);
            return changed;
        }

        float GetDeltaTime()
        {
            return ImGui::GetIO().DeltaTime;
        }

        // ---------------------------------------------------------------- グラフ

        void Plot(const char* function, const std::string& label, const CScriptArray& values, float minValue,
                  float maxValue, float height, bool histogram)
        {
            WidgetScope widget(function);
            if (!widget) {
                return;
            }
            const std::vector<float> data = ReadFloats(values);
            BeginLabeledRow(label);
            const std::string id = LabelFieldId(label);
            const ImVec2 size(0.0f, (std::max)(10.0f, height));
            if (histogram) {
                ImGui::PlotHistogram(id.c_str(), data.data(), static_cast<int>(data.size()), 0, nullptr, minValue, maxValue, size);
            } else {
                ImGui::PlotLines(id.c_str(), data.data(), static_cast<int>(data.size()), 0, nullptr, minValue, maxValue, size);
            }
        }

        void PlotLines(const std::string& label, const CScriptArray& values, float height)
        {
            Plot("PlotLines", label, values, FLT_MAX, FLT_MAX, height, false);
        }

        void PlotLinesRange(const std::string& label, const CScriptArray& values, float minValue, float maxValue, float height)
        {
            Plot("PlotLines", label, values, minValue, maxValue, height, false);
        }

        void PlotHistogram(const std::string& label, const CScriptArray& values, float height)
        {
            Plot("PlotHistogram", label, values, FLT_MAX, FLT_MAX, height, true);
        }

        void PlotHistogramRange(const std::string& label, const CScriptArray& values, float minValue, float maxValue, float height)
        {
            Plot("PlotHistogram", label, values, minValue, maxValue, height, true);
        }

        // ---------------------------------------------------------------- カーブ

        bool CurveField(const std::string& label, CScriptArray& keys, const Vector2& rangeMin, const Vector2& rangeMax, float height)
        {
            WidgetScope widget("CurveField");
            if (!widget) {
                return false;
            }
            const float spanX = rangeMax.x - rangeMin.x;
            const float spanY = rangeMax.y - rangeMin.y;
            if (!(spanX > 0.0f) || !(spanY > 0.0f)) {
                ThrowScriptException("EditorGUI::CurveField の範囲は、最大を最小より大きくします");
                return false;
            }

            BeginLabeledRow(label);
            const ImVec2 size((std::max)(40.0f, ImGui::CalcItemWidth()), (std::max)(30.0f, height));
            ImGui::PushID(LabelFieldId(label).c_str());
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 corner(origin.x + size.x, origin.y + size.y);
            ImGui::InvisibleButton("##curve", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const ImGuiID id = ImGui::GetItemID();
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();

            const auto toScreen = [&](const Vector2& point) {
                return ImVec2(origin.x + (point.x - rangeMin.x) / spanX * size.x, corner.y - (point.y - rangeMin.y) / spanY * size.y);
                };
            const auto toCurve = [&](const ImVec2& screen) {
                return Vector2{ std::clamp(rangeMin.x + (screen.x - origin.x) / size.x * spanX, rangeMin.x, rangeMax.x),
                                std::clamp(rangeMin.y + (corner.y - screen.y) / size.y * spanY, rangeMin.y, rangeMax.y) };
                };

            std::vector<Vector2> points = ReadVector2s(keys);
            const ImVec2 mouse = ImGui::GetMousePos();
            int hoveredKey = -1;
            float best = kHitRadius * kHitRadius;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const ImVec2 at = toScreen(points[i]);
                const float dx = at.x - mouse.x;
                const float dy = at.y - mouse.y;
                if (hovered && dx * dx + dy * dy <= best) {
                    best = dx * dx + dy * dy;
                    hoveredKey = static_cast<int>(i);
                }
            }

            // 左で点を掴んで動かし、何も無い所のダブルクリックで足し、右クリックで消す
            ImGuiStorage* const storage = ImGui::GetStateStorage();
            int dragging = storage->GetInt(id, -1);
            bool changed = false;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                dragging = hoveredKey;
            }
            if (hovered && hoveredKey < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                points.push_back(toCurve(mouse));
                dragging = static_cast<int>(points.size()) - 1;
                changed = true;
            }
            if (active && dragging >= 0 && dragging < static_cast<int>(points.size())
                && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                points[dragging] = toCurve(mouse);
                changed = true;
            }
            if (hovered && hoveredKey >= 0 && points.size() > 1 && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                points.erase(points.begin() + hoveredKey);
                hoveredKey = -1;
                dragging = -1;
                changed = true;
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                dragging = -1;
            }
            if (changed) {
                dragging = SortKeeping(points, dragging, [](const Vector2& a, const Vector2& b) { return a.x < b.x; });
                WriteVector2s(keys, points);
                MarkChanged(true);
            }
            storage->SetInt(id, dragging);

            // 背景・格子・曲線・点
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(origin, corner, Color(Theme::kField), 3.0f);
            for (int i = 1; i < 4; ++i) {
                const float x = origin.x + size.x * static_cast<float>(i) / 4.0f;
                const float y = origin.y + size.y * static_cast<float>(i) / 4.0f;
                drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, corner.y), Color(Theme::WithAlpha(Theme::kOutline, 0.6f)));
                drawList->AddLine(ImVec2(origin.x, y), ImVec2(corner.x, y), Color(Theme::WithAlpha(Theme::kOutline, 0.6f)));
            }
            drawList->PushClipRect(origin, corner, true);
            if (!points.empty()) {
                std::vector<ImVec2> line;
                line.reserve(kCurveSamples + 1);
                for (int i = 0; i <= kCurveSamples; ++i) {
                    const float x = rangeMin.x + spanX * static_cast<float>(i) / kCurveSamples;
                    line.push_back(toScreen(Vector2{ x, KeyCurve::Evaluate(points.data(), points.size(), x) }));
                }
                drawList->AddPolyline(line.data(), static_cast<int>(line.size()), Color(Theme::kAccentHover), ImDrawFlags_None, 2.0f);
            }
            for (std::size_t i = 0; i < points.size(); ++i) {
                const bool lit = static_cast<int>(i) == hoveredKey || static_cast<int>(i) == dragging;
                drawList->AddCircleFilled(toScreen(points[i]), kKeyRadius, Color(lit ? Theme::kWarm : Theme::kText), 12);
            }
            drawList->PopClipRect();
            drawList->AddRect(origin, corner, Color(Theme::kOutline), 3.0f);

            const int shown = dragging >= 0 ? dragging : hoveredKey;
            if (shown >= 0 && shown < static_cast<int>(points.size())) {
                ImGui::SetTooltip("%.3f, %.3f", points[shown].x, points[shown].y);
            }
            ImGui::PopID();
            return changed;
        }

        // ---------------------------------------------------------------- 時間軸

        struct TimelineState
        {
            bool open = false;
            float length = 1.0f;
            float snap = 0.0f;
            float time = 0.0f;
            ImVec2 origin{};
            float width = 0.0f;
            float labelWidth = 0.0f;
            float barLeft = 0.0f;
            float barWidth = 0.0f;
            int tracks = 0;
        };
        TimelineState g_timeline;

        float TimeToX(float time)
        {
            return g_timeline.barLeft + time / g_timeline.length * g_timeline.barWidth;
        }

        float SnapTime(float time)
        {
            if (g_timeline.snap > 0.0f) {
                time = std::round(time / g_timeline.snap) * g_timeline.snap;
            }
            return std::clamp(time, 0.0f, g_timeline.length);
        }

        float XToTime(float x)
        {
            return SnapTime((x - g_timeline.barLeft) / g_timeline.barWidth * g_timeline.length);
        }

        /// @brief 目盛りの間隔（1・2・5 × 10 のべき乗のうち、10 本ほどになるもの）
        float TickStep(float length)
        {
            const float raw = length / 10.0f;
            const float power = std::pow(10.0f, std::floor(std::log10(raw)));
            for (const float factor : { 1.0f, 2.0f, 5.0f, 10.0f }) {
                if (raw <= power * factor) {
                    return power * factor;
                }
            }
            return power * 10.0f;
        }

        bool RequireTimeline(const char* function)
        {
            if (!RequireGUI(function)) {
                return false;
            }
            if (!g_timeline.open) {
                ThrowScriptException(std::string("EditorGUI::") + function + " は BeginTimeline と EndTimeline の間で呼びます");
                return false;
            }
            return true;
        }

        /// @brief トラックの名前の列を描き、トラックの場所（目盛りの範囲）を押せるようにする
        /// @return トラックの上端
        float BeginTrack(const std::string& label)
        {
            const float top = ImGui::GetCursorScreenPos().y;
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            const ImVec2 rowMin(g_timeline.origin.x, top);
            const ImVec2 rowMax(g_timeline.origin.x + g_timeline.width, top + kTrackHeight);
            drawList->AddRectFilled(rowMin, rowMax, Color((g_timeline.tracks % 2) == 0 ? Theme::kChild : Theme::kWindow));
            drawList->PushClipRect(rowMin, ImVec2(g_timeline.origin.x + g_timeline.labelWidth, rowMax.y), true);
            drawList->AddText(ImVec2(rowMin.x + 4.0f, top + (kTrackHeight - ImGui::GetTextLineHeight()) * 0.5f),
                Color(Theme::kTextDim), LabelDisplayPart(label).c_str());
            drawList->PopClipRect();

            ImGui::PushID(g_timeline.tracks);
            ImGui::SetCursorScreenPos(ImVec2(g_timeline.barLeft - kHitRadius, top));
            ImGui::InvisibleButton("##track", ImVec2(g_timeline.barWidth + kHitRadius * 2.0f, kTrackHeight),
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            ++g_timeline.tracks;
            return top;
        }

        float BeginTimeline(const std::string& id, float length, float time, float snap)
        {
            if (!RequireGUI("BeginTimeline")) {
                return time;
            }
            if (g_timeline.open) {
                ThrowScriptException("EditorGUI::BeginTimeline の中に、もう 1 つの時間軸は置けません");
                return time;
            }
            if (!(length > 0.0f)) {
                ThrowScriptException("EditorGUI::BeginTimeline の長さは 0 より大きくします");
                return time;
            }

            ImGui::PushID(id.c_str());
            g_timeline = TimelineState{};
            g_timeline.open = true;
            g_timeline.length = length;
            g_timeline.snap = (std::max)(0.0f, snap);
            g_timeline.origin = ImGui::GetCursorScreenPos();
            g_timeline.width = (std::max)(160.0f, ImGui::GetContentRegionAvail().x);
            g_timeline.labelWidth = std::clamp(g_timeline.width * 0.28f, 60.0f, 140.0f);
            g_timeline.barLeft = g_timeline.origin.x + g_timeline.labelWidth + kTimelinePadding;
            g_timeline.barWidth = (std::max)(40.0f, g_timeline.width - g_timeline.labelWidth - kTimelinePadding * 2.0f);

            // 目盛りの行を押すかドラッグすると、再生位置が動く
            ImGui::SetCursorScreenPos(ImVec2(g_timeline.barLeft - kHitRadius, g_timeline.origin.y));
            ImGui::InvisibleButton("##ruler", ImVec2(g_timeline.barWidth + kHitRadius * 2.0f, kRulerHeight));
            float current = std::clamp(time, 0.0f, length);
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float scrubbed = XToTime(ImGui::GetMousePos().x);
                MarkChanged(scrubbed != current);
                current = scrubbed;
            }
            g_timeline.time = current;

            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            const ImVec2 rulerMin = g_timeline.origin;
            const ImVec2 rulerMax(g_timeline.origin.x + g_timeline.width, g_timeline.origin.y + kRulerHeight);
            drawList->AddRectFilled(rulerMin, rulerMax, Color(Theme::kPanel));
            char text[48];
            std::snprintf(text, sizeof(text), "%.2f / %.2f", current, length);
            drawList->AddText(ImVec2(rulerMin.x + 4.0f, rulerMin.y + (kRulerHeight - ImGui::GetTextLineHeight()) * 0.5f),
                Color(Theme::kText), text);

            const float step = TickStep(length);
            for (int i = 0; static_cast<float>(i) * step <= length + step * 0.001f; ++i) {
                const float t = static_cast<float>(i) * step;
                const float x = TimeToX(t);
                drawList->AddLine(ImVec2(x, rulerMax.y - 6.0f), ImVec2(x, rulerMax.y), Color(Theme::kTextMute));
                std::snprintf(text, sizeof(text), step < 1.0f ? "%.1f" : "%.0f", t);
                drawList->AddText(ImVec2(x + 2.0f, rulerMin.y + 2.0f), Color(Theme::kTextMute), text);
            }

            ImGui::SetCursorScreenPos(ImVec2(g_timeline.origin.x, rulerMax.y));
            PushGUIScope(GUIScopeKind::Timeline, true);
            return current;
        }

        bool TimelineKeys(const std::string& label, CScriptArray& keys)
        {
            if (!RequireTimeline("TimelineKeys")) {
                return false;
            }
            const float top = BeginTrack(label);
            const ImGuiID id = ImGui::GetItemID();
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            const float centerY = top + kTrackHeight * 0.5f;

            std::vector<float> times = ReadFloats(keys);
            const ImVec2 mouse = ImGui::GetMousePos();
            int hoveredKey = -1;
            float best = kHitRadius;
            for (std::size_t i = 0; i < times.size(); ++i) {
                const float distance = std::abs(TimeToX(times[i]) - mouse.x);
                if (hovered && distance <= best) {
                    best = distance;
                    hoveredKey = static_cast<int>(i);
                }
            }

            ImGuiStorage* const storage = ImGui::GetStateStorage();
            int dragging = storage->GetInt(id, -1);
            bool changed = false;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                dragging = hoveredKey;
            }
            if (hovered && hoveredKey < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                times.push_back(XToTime(mouse.x));
                dragging = static_cast<int>(times.size()) - 1;
                changed = true;
            }
            if (active && dragging >= 0 && dragging < static_cast<int>(times.size())
                && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                const float moved = XToTime(mouse.x);
                changed = changed || moved != times[dragging];
                times[dragging] = moved;
            }
            if (hovered && hoveredKey >= 0 && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                times.erase(times.begin() + hoveredKey);
                hoveredKey = -1;
                dragging = -1;
                changed = true;
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                dragging = -1;
            }
            if (changed) {
                dragging = SortKeeping(times, dragging, [](float a, float b) { return a < b; });
                WriteFloats(keys, times);
                MarkChanged(true);
            }
            storage->SetInt(id, dragging);

            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            for (std::size_t i = 0; i < times.size(); ++i) {
                const float x = TimeToX(times[i]);
                const bool lit = static_cast<int>(i) == hoveredKey || static_cast<int>(i) == dragging;
                drawList->AddQuadFilled(ImVec2(x, centerY - kKeyRadius - 1.0f), ImVec2(x + kKeyRadius + 1.0f, centerY),
                    ImVec2(x, centerY + kKeyRadius + 1.0f), ImVec2(x - kKeyRadius - 1.0f, centerY),
                    Color(lit ? Theme::kWarm : Theme::kAccentHover));
            }
            const int shown = dragging >= 0 ? dragging : hoveredKey;
            if (shown >= 0 && shown < static_cast<int>(times.size())) {
                ImGui::SetTooltip("%.3f", times[shown]);
            }
            ImGui::PopID();
            return changed;
        }

        bool TimelineRanges(const std::string& label, CScriptArray& ranges)
        {
            if (!RequireTimeline("TimelineRanges")) {
                return false;
            }
            const float top = BeginTrack(label);
            const ImGuiID id = ImGui::GetItemID();
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();

            std::vector<Vector2> spans = ReadVector2s(ranges);
            const ImVec2 mouse = ImGui::GetMousePos();
            const float minimum = g_timeline.snap > 0.0f ? g_timeline.snap : g_timeline.length * 0.01f;

            // 何を指しているか（0: 区間の中・1: 始まりの端・2: 終わりの端）
            int hoveredSpan = -1;
            int hoveredPart = 0;
            for (std::size_t i = 0; hovered && i < spans.size(); ++i) {
                const float start = TimeToX(spans[i].x);
                const float end = TimeToX(spans[i].y);
                if (std::abs(mouse.x - start) <= 5.0f) {
                    hoveredSpan = static_cast<int>(i);
                    hoveredPart = 1;
                } else if (std::abs(mouse.x - end) <= 5.0f) {
                    hoveredSpan = static_cast<int>(i);
                    hoveredPart = 2;
                } else if (mouse.x > start && mouse.x < end) {
                    hoveredSpan = static_cast<int>(i);
                    hoveredPart = 0;
                }
            }

            ImGuiStorage* const storage = ImGui::GetStateStorage();
            const ImGuiID partId = id + 1;
            const ImGuiID grabId = id + 2;
            int dragging = storage->GetInt(id, -1);
            int part = storage->GetInt(partId, 0);
            bool changed = false;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                dragging = hoveredSpan;
                part = hoveredPart;
                if (hoveredSpan >= 0) {
                    storage->SetFloat(grabId, XToTime(mouse.x) - spans[hoveredSpan].x);
                }
            }
            if (hovered && hoveredSpan < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                const float start = XToTime(mouse.x);
                const float span = g_timeline.snap > 0.0f ? g_timeline.snap * 4.0f : g_timeline.length * 0.1f;
                spans.push_back(Vector2{ start, (std::min)(g_timeline.length, start + span) });
                dragging = -1;
                changed = true;
            }
            if (active && dragging >= 0 && dragging < static_cast<int>(spans.size())
                && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                Vector2& span = spans[dragging];
                const Vector2 before = span;
                const float at = XToTime(mouse.x);
                if (part == 1) {
                    span.x = (std::min)(at, span.y - minimum);
                } else if (part == 2) {
                    span.y = (std::max)(at, span.x + minimum);
                } else {
                    const float duration = span.y - span.x;
                    const float start = std::clamp(SnapTime(at - storage->GetFloat(grabId, 0.0f)), 0.0f, g_timeline.length - duration);
                    span = Vector2{ start, start + duration };
                }
                span.x = std::clamp(span.x, 0.0f, g_timeline.length);
                span.y = std::clamp(span.y, span.x, g_timeline.length);
                changed = changed || before.x != span.x || before.y != span.y;
            }
            if (hovered && hoveredSpan >= 0 && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                spans.erase(spans.begin() + hoveredSpan);
                hoveredSpan = -1;
                dragging = -1;
                changed = true;
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                dragging = -1;
            }
            if (changed) {
                dragging = SortKeeping(spans, dragging, [](const Vector2& a, const Vector2& b) { return a.x < b.x; });
                WriteVector2s(ranges, spans);
                MarkChanged(true);
            }
            storage->SetInt(id, dragging);
            storage->SetInt(partId, part);

            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            for (std::size_t i = 0; i < spans.size(); ++i) {
                const bool lit = static_cast<int>(i) == hoveredSpan || static_cast<int>(i) == dragging;
                const ImVec2 min(TimeToX(spans[i].x), top + 4.0f);
                const ImVec2 max(TimeToX(spans[i].y), top + kTrackHeight - 4.0f);
                drawList->AddRectFilled(min, max, Color(Theme::WithAlpha(lit ? Theme::kWarm : Theme::kAccent, 0.75f)), 3.0f);
                drawList->AddRect(min, max, Color(lit ? Theme::kWarm : Theme::kAccentHover), 3.0f);
            }
            const int shown = dragging >= 0 ? dragging : hoveredSpan;
            if (shown >= 0 && shown < static_cast<int>(spans.size())) {
                ImGui::SetTooltip("%.3f 〜 %.3f", spans[shown].x, spans[shown].y);
            }
            ImGui::PopID();
            return changed;
        }

        void EndTimeline()
        {
            PopGUIScope(GUIScopeKind::Timeline, "EndTimeline");
        }
    }

    void CloseTimelineScope()
    {
        if (!g_timeline.open) {
            return;
        }
        // 再生位置の線を、目盛りから最後のトラックまで引く
        const float bottom = ImGui::GetCursorScreenPos().y;
        const float x = TimeToX(g_timeline.time);
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        drawList->AddLine(ImVec2(x, g_timeline.origin.y), ImVec2(x, bottom), Color(Theme::kWarm), 2.0f);
        drawList->AddTriangleFilled(ImVec2(x - 5.0f, g_timeline.origin.y), ImVec2(x + 5.0f, g_timeline.origin.y),
            ImVec2(x, g_timeline.origin.y + 7.0f), Color(Theme::kWarm));
        ImGui::Dummy(ImVec2(g_timeline.width, 2.0f));
        ImGui::PopID();
        g_timeline.open = false;
    }

    void RegisterEditorGUITools(Script::BindingRegistrar& r)
    {
        r.Function("bool DrawDefaultInspector()", asFUNCTION(DrawDefaultInspector));
        r.Function("float GetDeltaTime()", asFUNCTION(GetDeltaTime));

        r.Function("void PlotLines(const string &in label, const array<float> &in values, float height = 60)", asFUNCTION(PlotLines));
        r.Function("void PlotLines(const string &in label, const array<float> &in values, float min, float max, float height = 60)",
            asFUNCTION(PlotLinesRange));
        r.Function("void PlotHistogram(const string &in label, const array<float> &in values, float height = 60)",
            asFUNCTION(PlotHistogram));
        r.Function("void PlotHistogram(const string &in label, const array<float> &in values, float min, float max, float height = 60)",
            asFUNCTION(PlotHistogramRange));

        r.Function("bool CurveField(const string &in label, array<Vector2> &inout keys, const Vector2 &in rangeMin = Vector2(0.0f, 0.0f), const Vector2 &in rangeMax = Vector2(1.0f, 1.0f), float height = 110)",
            asFUNCTION(CurveField));

        r.Function("float BeginTimeline(const string &in id, float length, float time, float snap = 0)", asFUNCTION(BeginTimeline));
        r.Function("bool TimelineKeys(const string &in label, array<float> &inout keys)", asFUNCTION(TimelineKeys));
        r.Function("bool TimelineRanges(const string &in label, array<Vector2> &inout ranges)", asFUNCTION(TimelineRanges));
        r.Function("void EndTimeline()", asFUNCTION(EndTimeline));
    }
}

#endif // CORE_EDITOR
