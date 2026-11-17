#include "pch.h"
#include "AnimatorCanvas.h"

#ifdef CORE_EDITOR

#include "AnimatorLiveView.h"
#include "AnimatorWidgets.h"

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/DragDropPayload.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <utility>

namespace CoreEngine::Editor
{
    namespace
    {
        constexpr float kStateHalfWidth = 84.0f;
        constexpr float kStateHalfHeight = 23.0f;
        constexpr float kAnyHalfWidth = 64.0f;
        constexpr float kAnyHalfHeight = 17.0f;
        constexpr float kMinZoom = 0.3f;
        constexpr float kMaxZoom = 2.0f;
        constexpr float kGridStep = 32.0f;
        constexpr float kPickDistance = 6.0f;
        constexpr float kPairOffset = 7.0f;

        constexpr const char* kCanvasMenu = "##AnimatorCanvasMenu";

        ImU32 ToU32(const ImVec4& color)
        {
            return ImGui::ColorConvertFloat4ToU32(color);
        }

        ImVec2 Add(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
        ImVec2 Sub(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x - b.x, a.y - b.y); }
        ImVec2 Scale(const ImVec2& v, float s) { return ImVec2(v.x * s, v.y * s); }
        float Length(const ImVec2& v) { return std::sqrt(v.x * v.x + v.y * v.y); }

        float DistanceToSegment(const ImVec2& point, const ImVec2& a, const ImVec2& b)
        {
            const ImVec2 ab = Sub(b, a);
            const float lengthSq = ab.x * ab.x + ab.y * ab.y;
            float t = lengthSq > 0.0f ? ((point.x - a.x) * ab.x + (point.y - a.y) * ab.y) / lengthSq : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            return Length(Sub(point, Add(a, Scale(ab, t))));
        }

        /// @brief 箱の中心から向きの方へ伸ばした線が箱の縁と交わる点
        ImVec2 EdgePoint(const ImVec2& center, const ImVec2& half, const ImVec2& direction)
        {
            float t = FLT_MAX;
            if (std::fabs(direction.x) > 1e-6f) {
                t = std::min(t, half.x / std::fabs(direction.x));
            }
            if (std::fabs(direction.y) > 1e-6f) {
                t = std::min(t, half.y / std::fabs(direction.y));
            }
            return t == FLT_MAX ? center : Add(center, Scale(direction, t));
        }
    }

    ImVec2 AnimatorCanvas::ToScreen(const AnimatorDocument& document, const ImVec2& canvasMin, const Vector2& world) const
    {
        return ImVec2(canvasMin.x + document.pan.x + world.x * document.zoom,
            canvasMin.y + document.pan.y + world.y * document.zoom);
    }

    Vector2 AnimatorCanvas::ToWorld(const AnimatorDocument& document, const ImVec2& canvasMin, const ImVec2& screen) const
    {
        return Vector2{ (screen.x - canvasMin.x - document.pan.x) / document.zoom,
            (screen.y - canvasMin.y - document.pan.y) / document.zoom };
    }

    Vector2 AnimatorCanvas::NodePosition(const AnimatorDocument& document, AnimatorSelection kind, int index) const
    {
        const AnimatorLayer& layer = document.data.layers.front();
        if (kind == AnimatorSelection::AnyState) {
            return layer.anyStatePosition;
        }
        if (kind == AnimatorSelection::State && index >= 0 && index < static_cast<int>(layer.states.size())) {
            return layer.states[static_cast<size_t>(index)].position;
        }
        return Vector2{};
    }

    bool AnimatorCanvas::TransitionSegment(const AnimatorDocument& document, const ImVec2& canvasMin, int transitionIndex,
        ImVec2& start, ImVec2& end) const
    {
        const AnimatorLayer& layer = document.data.layers.front();
        const AnimatorTransition& transition = layer.transitions[static_cast<size_t>(transitionIndex)];
        const bool fromAny = transition.from == AnimatorControllerFormat::kAnyState;
        const int fromIndex = fromAny ? -1 : AnimatorStateIndexOf(layer, transition.from);
        const int toIndex = AnimatorStateIndexOf(layer, transition.to);
        if ((!fromAny && fromIndex < 0) || toIndex < 0 || fromIndex == toIndex) {
            return false;
        }
        const Vector2 fromWorld = fromAny ? layer.anyStatePosition : layer.states[static_cast<size_t>(fromIndex)].position;
        const ImVec2 a = ToScreen(document, canvasMin, fromWorld);
        const ImVec2 b = ToScreen(document, canvasMin, layer.states[static_cast<size_t>(toIndex)].position);
        const ImVec2 delta = Sub(b, a);
        const float length = Length(delta);
        if (length < 1.0f) {
            return false;
        }
        const ImVec2 direction = Scale(delta, 1.0f / length);

        // 逆向きの遷移があれば、向きの左へずらして 2 本を並べる
        const bool reverse = std::any_of(layer.transitions.begin(), layer.transitions.end(),
            [&](const AnimatorTransition& other) { return other.from == transition.to && other.to == transition.from; });
        const ImVec2 shift = reverse ? Scale(ImVec2(-direction.y, direction.x), kPairOffset * document.zoom) : ImVec2(0.0f, 0.0f);

        const float zoom = document.zoom;
        const ImVec2 fromHalf = fromAny ? ImVec2(kAnyHalfWidth * zoom, kAnyHalfHeight * zoom)
            : ImVec2(kStateHalfWidth * zoom, kStateHalfHeight * zoom);
        const ImVec2 toHalf(kStateHalfWidth * zoom, kStateHalfHeight * zoom);
        start = EdgePoint(Add(a, shift), fromHalf, direction);
        end = EdgePoint(Add(b, shift), toHalf, Scale(direction, -1.0f));
        return true;
    }

    AnimatorHit AnimatorCanvas::HitTest(const AnimatorDocument& document, const ImVec2& canvasMin, const ImVec2& mouse) const
    {
        const AnimatorLayer& layer = document.data.layers.front();
        const float zoom = document.zoom;
        const auto inside = [&](const Vector2& world, float halfWidth, float halfHeight) {
            const ImVec2 center = ToScreen(document, canvasMin, world);
            return std::fabs(mouse.x - center.x) <= halfWidth * zoom && std::fabs(mouse.y - center.y) <= halfHeight * zoom;
            };
        for (int i = static_cast<int>(layer.states.size()) - 1; i >= 0; --i) {
            if (inside(layer.states[static_cast<size_t>(i)].position, kStateHalfWidth, kStateHalfHeight)) {
                return AnimatorHit{ AnimatorSelection::State, i };
            }
        }
        if (inside(layer.anyStatePosition, kAnyHalfWidth, kAnyHalfHeight)) {
            return AnimatorHit{ AnimatorSelection::AnyState, 0 };
        }
        for (int t = 0; t < static_cast<int>(layer.transitions.size()); ++t) {
            ImVec2 start, end;
            if (TransitionSegment(document, canvasMin, t, start, end) && DistanceToSegment(mouse, start, end) <= kPickDistance) {
                return AnimatorHit{ AnimatorSelection::Transition, t };
            }
        }
        return AnimatorHit{};
    }

    void AnimatorCanvas::FrameAll(AnimatorDocument& document, const ImVec2& canvasSize) const
    {
        const AnimatorLayer& layer = document.data.layers.front();
        float minX = layer.anyStatePosition.x - kAnyHalfWidth;
        float maxX = layer.anyStatePosition.x + kAnyHalfWidth;
        float minY = layer.anyStatePosition.y - kAnyHalfHeight;
        float maxY = layer.anyStatePosition.y + kAnyHalfHeight;
        for (const AnimatorState& state : layer.states) {
            minX = std::min(minX, state.position.x - kStateHalfWidth);
            maxX = std::max(maxX, state.position.x + kStateHalfWidth);
            minY = std::min(minY, state.position.y - kStateHalfHeight);
            maxY = std::max(maxY, state.position.y + kStateHalfHeight);
        }
        const float width = std::max(1.0f, maxX - minX);
        const float height = std::max(1.0f, maxY - minY);
        document.zoom = std::clamp(std::min(canvasSize.x * 0.85f / width, canvasSize.y * 0.85f / height), kMinZoom, 1.0f);
        const float centerX = (minX + maxX) * 0.5f;
        const float centerY = (minY + maxY) * 0.5f;
        document.pan = ImVec2(canvasSize.x * 0.5f - centerX * document.zoom, canvasSize.y * 0.5f - centerY * document.zoom);
    }

    void AnimatorCanvas::Draw(AnimatorDocument& document, const AnimatorLiveView& live)
    {
        AnimatorLayer& layer = document.data.layers.front();
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        if (canvasSize.x < 16.0f || canvasSize.y < 16.0f) {
            return;
        }
        const ImVec2 canvasMax = Add(canvasMin, canvasSize);
        if (!document.viewInitialized || document.frameRequested) {
            FrameAll(document, canvasSize);
            document.viewInitialized = true;
            document.frameRequested = false;
        }

        ImGui::InvisibleButton("##canvas", canvasSize,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 mouse = io.MousePos;

        // Project のモデルか、左の表のクリップを落とすと状態を作る
        if (ImGui::BeginDragDropTarget()) {
            const Vector2 world = ToWorld(document, canvasMin, mouse);
            if (const std::optional<std::string> model = AnimatorWidgets::AcceptModelDrop()) {
                document.AddClipsFromModel(*model, &world);
            }
            if (const std::optional<std::string> clip = UI::AcceptPayload<std::string>(AnimatorWidgets::kClipPayload)) {
                document.Select(AnimatorSelection::State, document.AddState(world, *clip));
            }
            ImGui::EndDragDropTarget();
        }

        const AnimatorHit hover = hovered ? HitTest(document, canvasMin, mouse) : AnimatorHit{};

        // 拡大縮小（マウスの下の点を動かさない）
        if (hovered && io.MouseWheel != 0.0f) {
            const Vector2 before = ToWorld(document, canvasMin, mouse);
            document.zoom = std::clamp(document.zoom * std::pow(1.1f, io.MouseWheel), kMinZoom, kMaxZoom);
            document.pan = ImVec2(mouse.x - canvasMin.x - before.x * document.zoom, mouse.y - canvasMin.y - before.y * document.zoom);
        }
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
            document.pan = Add(document.pan, io.MouseDelta);
        }

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (document.linkFrom) {
                if (hover.kind == AnimatorSelection::State) {
                    document.AddTransition(*document.linkFrom, hover.index);
                }
                document.linkFrom.reset();
            } else if (hover.kind == AnimatorSelection::State || hover.kind == AnimatorSelection::AnyState) {
                document.Select(hover.kind, hover.index);
                document.dragging = hover;
                const Vector2 position = NodePosition(document, hover.kind, hover.index);
                const Vector2 world = ToWorld(document, canvasMin, mouse);
                document.dragOffset = ImVec2(position.x - world.x, position.y - world.y);
            } else if (hover.kind == AnimatorSelection::Transition) {
                document.Select(AnimatorSelection::Transition, hover.index);
            } else {
                document.Select(AnimatorSelection::None, -1);
                document.panning = true;
            }
        }
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            if (document.dragging.kind == AnimatorSelection::State || document.dragging.kind == AnimatorSelection::AnyState) {
                const Vector2 world = ToWorld(document, canvasMin, mouse);
                const Vector2 target{ std::round(world.x + document.dragOffset.x), std::round(world.y + document.dragOffset.y) };
                Vector2* position = nullptr;
                if (document.dragging.kind == AnimatorSelection::AnyState) {
                    position = &layer.anyStatePosition;
                } else if (document.dragging.index < static_cast<int>(layer.states.size())) {
                    position = &layer.states[static_cast<size_t>(document.dragging.index)].position;
                }
                if (position && (position->x != target.x || position->y != target.y)) {
                    *position = target;
                    document.MarkChanged("状態を動かす");
                }
            } else if (document.panning) {
                document.pan = Add(document.pan, io.MouseDelta);
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            document.dragging = {};
            document.panning = false;
        }

        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            if (document.linkFrom) {
                document.linkFrom.reset();
            } else {
                document.contextHit = hover;
                const Vector2 world = ToWorld(document, canvasMin, mouse);
                document.contextWorld = ImVec2(world.x, world.y);
                if (hover.kind != AnimatorSelection::None) {
                    document.Select(hover.kind, hover.index);
                }
                ImGui::OpenPopup(kCanvasMenu);
            }
        }
        if (ImGui::IsWindowFocused() && !io.WantTextInput) {
            if (document.linkFrom && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                document.linkFrom.reset();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                document.DeleteSelection();
            }
        }

        // ===== 描く =====
        drawList->PushClipRect(canvasMin, canvasMax, true);
        drawList->AddRectFilled(canvasMin, canvasMax, ToU32(Theme::kDeepest));
        const float step = kGridStep * document.zoom;
        if (step >= 8.0f) {
            const ImU32 gridColor = ToU32(Theme::WithAlpha(Theme::kOutline, 0.45f));
            for (float x = std::fmod(document.pan.x, step); x < canvasSize.x; x += step) {
                drawList->AddLine(ImVec2(canvasMin.x + x, canvasMin.y), ImVec2(canvasMin.x + x, canvasMax.y), gridColor);
            }
            for (float y = std::fmod(document.pan.y, step); y < canvasSize.y; y += step) {
                drawList->AddLine(ImVec2(canvasMin.x, canvasMin.y + y), ImVec2(canvasMax.x, canvasMin.y + y), gridColor);
            }
        }

        ImFont* const font = ImGui::GetFont();
        const float zoom = document.zoom;
        const float fontSize = ImGui::GetFontSize() * std::clamp(zoom, 0.5f, 1.6f);
        const float thicknessScale = std::clamp(zoom, 0.7f, 1.4f);

        // 遷移（同じ元と先の組が何本あるかを数え、1 本目に数を添える）
        for (int t = 0; t < static_cast<int>(layer.transitions.size()); ++t) {
            ImVec2 start, end;
            if (!TransitionSegment(document, canvasMin, t, start, end)) {
                continue;
            }
            const AnimatorTransition& transition = layer.transitions[static_cast<size_t>(t)];
            int sameBefore = 0;
            int sameCount = 0;
            for (int other = 0; other < static_cast<int>(layer.transitions.size()); ++other) {
                const AnimatorTransition& candidate = layer.transitions[static_cast<size_t>(other)];
                if (candidate.from == transition.from && candidate.to == transition.to) {
                    ++sameCount;
                    if (other < t) {
                        ++sameBefore;
                    }
                }
            }
            const bool selected = document.selection == AnimatorSelection::Transition && document.selected == t;
            const bool liveTransition = live.inTransition && !live.transitionTo.empty()
                && transition.from == live.transitionFrom && transition.to == live.transitionTo && sameBefore == 0;
            ImVec4 color = Theme::kTextDim;
            if (liveTransition) {
                color = Theme::kOk;
            }
            if (hover.kind == AnimatorSelection::Transition && hover.index == t) {
                color = Theme::kAccentHover;
            }
            if (selected) {
                color = Theme::kWarm;
            }
            const float thickness = (selected || liveTransition ? 3.0f : 2.0f) * thicknessScale;
            drawList->AddLine(start, end, ToU32(color), thickness);

            const ImVec2 delta = Sub(end, start);
            const float length = Length(delta);
            if (length > 1.0f) {
                const ImVec2 direction = Scale(delta, 1.0f / length);
                const ImVec2 normal(-direction.y, direction.x);
                const ImVec2 middle = Add(start, Scale(delta, liveTransition ? std::clamp(live.transitionProgress, 0.0f, 1.0f) * 0.6f + 0.2f : 0.5f));
                const float size = 6.0f * thicknessScale;
                drawList->AddTriangleFilled(Add(middle, Scale(direction, size)),
                    Add(Sub(middle, Scale(direction, size)), Scale(normal, size)),
                    Sub(Sub(middle, Scale(direction, size)), Scale(normal, size)), ToU32(color));
                if (sameCount > 1 && sameBefore == 0) {
                    const std::string count = "×" + std::to_string(sameCount);
                    drawList->AddText(font, fontSize * 0.85f, Add(middle, Scale(normal, size + 4.0f)), ToU32(color), count.c_str());
                }
            }
        }

        // 遷移を引いている途中の線
        if (document.linkFrom) {
            const bool fromAny = *document.linkFrom < 0;
            const Vector2 world = fromAny ? layer.anyStatePosition : NodePosition(document, AnimatorSelection::State, *document.linkFrom);
            drawList->AddLine(ToScreen(document, canvasMin, world), mouse, ToU32(Theme::kOk), 2.0f * thicknessScale);
            drawList->AddCircleFilled(mouse, 4.0f, ToU32(Theme::kOk));
        }

        const auto drawBox = [&](const Vector2& world, float halfWidth, float halfHeight, const ImVec4& fill,
            const ImVec4& outline, float outlineThickness, const std::string& title, const std::string& sub) {
            const ImVec2 center = ToScreen(document, canvasMin, world);
            const ImVec2 half(halfWidth * zoom, halfHeight * zoom);
            const ImVec2 boxMin = Sub(center, half);
            const ImVec2 boxMax = Add(center, half);
            const float rounding = 6.0f * zoom;
            drawList->AddRectFilled(boxMin, boxMax, ToU32(fill), rounding);
            drawList->AddRect(boxMin, boxMax, ToU32(outline), rounding, 0, outlineThickness);

            const ImVec4 clip(boxMin.x + 4.0f, boxMin.y, boxMax.x - 4.0f, boxMax.y);
            const bool showSub = !sub.empty() && zoom >= 0.6f;
            const ImVec2 titleSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, title.c_str());
            const float subSize = fontSize * 0.8f;
            const float totalHeight = titleSize.y + (showSub ? subSize : 0.0f);
            float y = center.y - totalHeight * 0.5f;
            drawList->AddText(font, fontSize, ImVec2(std::max(boxMin.x + 4.0f, center.x - titleSize.x * 0.5f), y),
                ToU32(Theme::kText), title.c_str(), nullptr, 0.0f, &clip);
            if (showSub) {
                y += titleSize.y;
                const ImVec2 subExtent = font->CalcTextSizeA(subSize, FLT_MAX, 0.0f, sub.c_str());
                drawList->AddText(font, subSize, ImVec2(std::max(boxMin.x + 4.0f, center.x - subExtent.x * 0.5f), y),
                    ToU32(Theme::kTextDim), sub.c_str(), nullptr, 0.0f, &clip);
            }
            return std::pair<ImVec2, ImVec2>(boxMin, boxMax);
            };

        // どこからでも
        {
            const bool selected = document.selection == AnimatorSelection::AnyState;
            const bool hot = hover.kind == AnimatorSelection::AnyState;
            const ImVec4 outline = selected ? Theme::kAccentHover : (hot ? Theme::kTextDim : Theme::kOutline);
            drawBox(layer.anyStatePosition, kAnyHalfWidth, kAnyHalfHeight, Theme::kGraphUV, outline,
                selected ? 2.5f : 1.5f, "どこからでも", "");
        }

        // 状態
        for (int i = 0; i < static_cast<int>(layer.states.size()); ++i) {
            const AnimatorState& state = layer.states[static_cast<size_t>(i)];
            const bool isDefault = state.name == layer.defaultState;
            const bool selected = document.selection == AnimatorSelection::State && document.selected == i;
            const bool hot = hover.kind == AnimatorSelection::State && hover.index == i;
            const bool liveCurrent = live.runtime && state.name == live.currentState;
            const bool liveSource = live.runtime && live.inTransition && state.name == live.sourceState;

            ImVec4 fill = isDefault ? Theme::kGraphArtistic : Theme::kControl;
            if (liveCurrent) {
                fill = Theme::kAccentMuted;
            }
            ImVec4 outline = Theme::kOutline;
            float outlineThickness = 1.5f;
            if (liveSource) {
                outline = Theme::WithAlpha(Theme::kOk, 0.55f);
                outlineThickness = 2.0f;
            }
            if (liveCurrent) {
                outline = Theme::kOk;
                outlineThickness = 2.5f;
            }
            if (hot) {
                outline = document.linkFrom ? Theme::kOk : Theme::kTextDim;
            }
            if (selected) {
                outline = Theme::kAccentHover;
                outlineThickness = 2.5f;
            }
            std::string sub = state.clip.empty() ? std::string("（クリップなし）") : state.clip;
            if (!state.clip.empty() && !document.data.FindClip(state.clip)) {
                sub = "！" + state.clip + " がない";
            }
            const auto [boxMin, boxMax] = drawBox(state.position, kStateHalfWidth, kStateHalfHeight, fill, outline,
                outlineThickness, state.name, sub);

            // 再生中は今の状態の下に進みの帯を出す
            if (liveCurrent) {
                const float barHeight = std::max(2.0f, 3.0f * zoom);
                const float progress = std::clamp(live.normalizedTime, 0.0f, 1.0f);
                const ImVec2 barMin(boxMin.x + 4.0f * zoom, boxMax.y - barHeight - 3.0f * zoom);
                const ImVec2 barMax(boxMax.x - 4.0f * zoom, boxMax.y - 3.0f * zoom);
                drawList->AddRectFilled(barMin, barMax, ToU32(Theme::WithAlpha(Theme::kDeepest, 0.8f)));
                drawList->AddRectFilled(barMin, ImVec2(barMin.x + (barMax.x - barMin.x) * progress, barMax.y), ToU32(Theme::kOk));
            }
        }

        // 案内
        const ImVec2 hintPosition(canvasMin.x + 8.0f, canvasMin.y + 6.0f);
        if (document.linkFrom) {
            drawList->AddText(hintPosition, ToU32(Theme::kOk), "移る先の状態をクリック（右クリックか Esc でやめる）");
        } else if (layer.states.empty()) {
            drawList->AddText(hintPosition, ToU32(Theme::kTextMute),
                "右クリック > 状態を作る。Project のモデルをここへ落とすと、中のアニメーションごとに状態を作ります。");
        }
        drawList->PopClipRect();

        DrawMenu(document);
    }

    void AnimatorCanvas::DrawMenu(AnimatorDocument& document)
    {
        if (!ImGui::BeginPopup(kCanvasMenu)) {
            return;
        }
        AnimatorLayer& layer = document.data.layers.front();
        const AnimatorHit hit = document.contextHit;
        const Vector2 world{ document.contextWorld.x, document.contextWorld.y };
        if (hit.kind == AnimatorSelection::State && hit.index >= 0 && hit.index < static_cast<int>(layer.states.size())) {
            const bool isDefault = layer.states[static_cast<size_t>(hit.index)].name == layer.defaultState;
            if (ImGui::MenuItem("遷移を引く")) {
                document.linkFrom = hit.index;
            }
            if (ImGui::MenuItem("最初の状態にする", nullptr, false, !isDefault)) {
                document.SetDefaultState(hit.index);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("消す", "Delete")) {
                document.DeleteState(hit.index);
            }
        } else if (hit.kind == AnimatorSelection::AnyState) {
            if (ImGui::MenuItem("遷移を引く")) {
                document.linkFrom = -1;
            }
        } else if (hit.kind == AnimatorSelection::Transition && hit.index >= 0 && hit.index < static_cast<int>(layer.transitions.size())) {
            if (ImGui::MenuItem("消す", "Delete")) {
                document.DeleteTransition(hit.index);
            }
        } else {
            if (ImGui::MenuItem("状態を作る")) {
                document.Select(AnimatorSelection::State, document.AddState(world, ""));
            }
            if (ImGui::BeginMenu("クリップから状態を作る", !document.data.clips.empty())) {
                for (size_t i = 0; i < document.data.clips.size(); ++i) {
                    const std::string id = document.data.clips[i].id;
                    if (ImGui::MenuItem(id.c_str())) {
                        document.Select(AnimatorSelection::State, document.AddState(world, id));
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("全体を見る")) {
                document.frameRequested = true;
            }
        }
        ImGui::EndPopup();
    }
}

#endif // CORE_EDITOR
