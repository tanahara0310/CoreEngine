#include "pch.h"
#include "Editor/Inspector/RectTransformInspector.h"

#ifdef CORE_EDITOR

#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Inspector/InspectorLayout.h"
#include "Editor/Inspector/InspectorRenderer.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "GameObject/GameObject.h"
#include "Reflection/TypeDescriptor.h"
#include "UI/RectTransformComponent.h"

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace CoreEngine::Editor::RectTransformInspector
{
    namespace
    {
        /// @brief 1 つの向きのアンカーの選び方
        struct AxisPreset
        {
            const char* name; ///< 表示名
            float lo;         ///< アンカーの小さい側（0 = 左・上）
            float hi;         ///< アンカーの大きい側
            float pivot;      ///< Shift で合わせる基準点
        };

        /// 横の選び方（左・中央・右・横いっぱい）
        constexpr std::array<AxisPreset, 4> kHorizontal = { {
            { "左", 0.0f, 0.0f, 0.0f },
            { "中央", 0.5f, 0.5f, 0.5f },
            { "右", 1.0f, 1.0f, 1.0f },
            { "横いっぱい", 0.0f, 1.0f, 0.5f },
        } };

        /// 縦の選び方（上・中央・下・縦いっぱい）
        constexpr std::array<AxisPreset, 4> kVertical = { {
            { "上", 0.0f, 0.0f, 0.0f },
            { "中央", 0.5f, 0.5f, 0.5f },
            { "下", 1.0f, 1.0f, 1.0f },
            { "縦いっぱい", 0.0f, 1.0f, 0.5f },
        } };

        /// アンカーの格子のポップアップの ID
        constexpr const char* kPresetPopupId = "##anchorPresets";

        /// 格子の 1 マスの大きさ（px）
        constexpr float kCellSize = 40.0f;

        /// 格子のマスの間（px）
        constexpr float kCellGap = 4.0f;

        /// @brief 今のアンカーに合う選び方の番号（無ければ -1）
        int FindAxisPreset(const std::array<AxisPreset, 4>& presets, float lo, float hi)
        {
            for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
                if (presets[i].lo == lo && presets[i].hi == hi) {
                    return i;
                }
            }
            return -1;
        }

        /// @brief 2 つの配置が同じか
        bool SameLayout(const UILayout& a, const UILayout& b)
        {
            return a.anchorMin.x == b.anchorMin.x && a.anchorMin.y == b.anchorMin.y
                && a.anchorMax.x == b.anchorMax.x && a.anchorMax.y == b.anchorMax.y
                && a.anchoredPos.x == b.anchoredPos.x && a.anchoredPos.y == b.anchoredPos.y
                && a.pivot.x == b.pivot.x && a.pivot.y == b.pivot.y
                && a.size.x == b.size.x && a.size.y == b.size.y
                && a.rotation == b.rotation
                && a.scale.x == b.scale.x && a.scale.y == b.scale.y
                && a.sortOrder == b.sortOrder;
        }

        /// @brief 履歴に出す名前（「オブジェクト名 の 何を」）
        std::string MakeLabel(const RectTransformComponent& rect, const char* what)
        {
            const GameObject* const owner = rect.GetOwner();
            return (owner ? owner->GetName() + " の " : std::string{}) + what;
        }

        /// @brief 配置の変更を、控えた配置へ戻す・やり直すコマンドとして履歴へ積む
        void PushLayoutCommand(RectTransformComponent& rect, const UILayout& before, std::string label)
        {
            EditorCommandStack::Get().Push(
                std::make_unique<ComponentStateCommand<RectTransformComponent, UILayout>>(
                    std::move(label), rect, before,
                    [](RectTransformComponent& target) { return target.GetLayout(); },
                    [](RectTransformComponent& target, const UILayout& layout) { target.SetLayout(layout); }));
        }

        /// @brief 画面上の四角を動かさずに基準点を変える
        void SetPivotKeepingRect(RectTransformComponent& rect, const Vector2& pivot, const Vector2& canvasSize)
        {
            UIRect parentRect;
            Matrix4x4 parentMatrix;
            rect.ComputeParentFrame(canvasSize, parentRect, parentMatrix);

            UILayout layout = rect.GetLayout();
            const Vector2 parentSize = parentRect.Size();
            const Vector2 actual = layout.CalculateSize(parentRect);
            const Vector2 delta = { pivot.x - layout.pivot.x, pivot.y - layout.pivot.y };

            // 基準点が自分の中で動く量を、拡大と回転を掛けて親の座標へ移す
            const Vector2 local = { delta.x * actual.x * layout.scale.x, delta.y * actual.y * layout.scale.y };
            const float cosR = std::cos(layout.rotation);
            const float sinR = std::sin(layout.rotation);
            const Vector2 moved = { local.x * cosR - local.y * sinR, local.x * sinR + local.y * cosR };

            // 開いたアンカーの基準の点が基準点に合わせて動く分を引く
            layout.anchoredPos.x += moved.x - parentSize.x * (layout.anchorMax.x - layout.anchorMin.x) * delta.x;
            layout.anchoredPos.y += moved.y - parentSize.y * (layout.anchorMax.y - layout.anchorMin.y) * delta.y;
            layout.pivot = pivot;
            rect.SetLayout(layout);
        }

        /// @brief 選んだアンカーにする（画面上の四角は保つ）
        /// @param withPivot 基準点も選んだ所へ合わせる
        /// @param withPosition 位置も選んだ所へ合わせる（伸ばす向きは親いっぱいにする）
        /// @return 変えたら true
        bool ApplyPreset(RectTransformComponent& rect, const AxisPreset& horizontal, const AxisPreset& vertical,
                         bool withPivot, bool withPosition)
        {
            const Vector2 canvasSize = rect.FindCanvasSize();
            const UILayout before = rect.GetLayout();

            rect.ChangeAnchorsKeepingRect({ horizontal.lo, vertical.lo }, { horizontal.hi, vertical.hi }, canvasSize);
            if (withPivot) {
                SetPivotKeepingRect(rect, { horizontal.pivot, vertical.pivot }, canvasSize);
            }
            if (withPosition) {
                UILayout layout = rect.GetLayout();
                layout.anchoredPos = { 0.0f, 0.0f };
                if (layout.IsStretchedX()) {
                    layout.size.x = 0.0f;
                }
                if (layout.IsStretchedY()) {
                    layout.size.y = 0.0f;
                }
                rect.SetLayout(layout);
            }

            if (SameLayout(before, rect.GetLayout())) {
                return false;
            }
            PushLayoutCommand(rect, before, MakeLabel(rect, "アンカー"));
            return true;
        }

        /// @brief 1 つの向きで、親の枠の中に置く子の四角の範囲
        std::pair<float, float> GlyphSpan(float parentMin, float parentMax, const AxisPreset& preset)
        {
            constexpr float kStretchInset = 2.0f;
            constexpr float kPointRatio = 0.42f;
            if (preset.lo != preset.hi) {
                return { parentMin + kStretchInset, parentMax - kStretchInset };
            }
            const float length = parentMax - parentMin;
            const float width = length * kPointRatio;
            const float start = parentMin + length * preset.lo - width * preset.lo;
            return { start, start + width };
        }

        /// @brief 格子の 1 マス（親の枠・子の四角・アンカーの点）を描く
        void DrawPresetGlyph(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
                             const AxisPreset& horizontal, const AxisPreset& vertical, bool selected, bool hovered)
        {
            namespace Theme = Editor::Theme;
            const ImU32 background = ImGui::GetColorU32(selected ? Theme::WithAlpha(Theme::kAccent, 0.45f)
                : hovered ? ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered)
                : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            drawList->AddRectFilled(min, max, background, 3.0f);

            constexpr float kParentInset = 8.0f;
            const ImVec2 parentMin(min.x + kParentInset, min.y + kParentInset);
            const ImVec2 parentMax(max.x - kParentInset, max.y - kParentInset);
            drawList->AddRect(parentMin, parentMax, ImGui::GetColorU32(Theme::kTextMute));

            const auto [left, right] = GlyphSpan(parentMin.x, parentMax.x, horizontal);
            const auto [top, bottom] = GlyphSpan(parentMin.y, parentMax.y, vertical);
            drawList->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), ImGui::GetColorU32(Theme::kTextDim));

            // アンカーの点（開いた向きは両端に打つ）
            const ImU32 anchorColor = ImGui::GetColorU32(Theme::kWarm);
            const float width = parentMax.x - parentMin.x;
            const float height = parentMax.y - parentMin.y;
            for (const float u : { horizontal.lo, horizontal.hi }) {
                for (const float v : { vertical.lo, vertical.hi }) {
                    drawList->AddCircleFilled(ImVec2(parentMin.x + width * u, parentMin.y + height * v), 2.5f, anchorColor);
                }
            }
        }

        /// @brief アンカーの選択の行（押すと 16 通りの格子が開く）
        bool DrawPresetRow(RectTransformComponent& rect)
        {
            const UILayout& layout = rect.GetLayout();
            const int currentH = FindAxisPreset(kHorizontal, layout.anchorMin.x, layout.anchorMax.x);
            const int currentV = FindAxisPreset(kVertical, layout.anchorMin.y, layout.anchorMax.y);
            const std::string current = (currentH >= 0 && currentV >= 0)
                ? std::string("横: ") + kHorizontal[currentH].name + "　縦: " + kVertical[currentV].name
                : std::string("そのほか（下の数で決めている）");

            InspectorLayout::BeginRow("アンカーの選択", Editor::Theme::kTextDim);
            if (ImGui::Button((current + "##anchorPreset").c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                ImGui::OpenPopup(kPresetPopupId);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("押すと 16 通りから選べる（Shift で基準点も、Alt で位置も合わせる）");
            }

            bool changed = false;
            if (ImGui::BeginPopup(kPresetPopupId)) {
                const ImGuiIO& io = ImGui::GetIO();
                ImGui::TextColored(io.KeyShift ? Editor::Theme::kWarm : Editor::Theme::kTextDim,
                    "Shift を押しながら：基準点も合わせる");
                ImGui::TextColored(io.KeyAlt ? Editor::Theme::kWarm : Editor::Theme::kTextDim,
                    "Alt を押しながら：位置も合わせる");
                UI::Separator();

                ImDrawList* const drawList = ImGui::GetWindowDrawList();
                for (int row = 0; row < static_cast<int>(kVertical.size()); ++row) {
                    for (int column = 0; column < static_cast<int>(kHorizontal.size()); ++column) {
                        ImGui::PushID(row * static_cast<int>(kHorizontal.size()) + column);
                        if (column > 0) {
                            ImGui::SameLine(0.0f, kCellGap);
                        }
                        const ImVec2 min = ImGui::GetCursorScreenPos();
                        const bool pressed = ImGui::InvisibleButton("##cell", ImVec2(kCellSize, kCellSize));
                        const bool hovered = ImGui::IsItemHovered();
                        const AxisPreset& horizontal = kHorizontal[column];
                        const AxisPreset& vertical = kVertical[row];
                        DrawPresetGlyph(drawList, min, ImVec2(min.x + kCellSize, min.y + kCellSize),
                            horizontal, vertical, row == currentV && column == currentH, hovered);
                        if (hovered) {
                            ImGui::SetTooltip("横: %s　縦: %s", horizontal.name, vertical.name);
                        }
                        if (pressed) {
                            changed |= ApplyPreset(rect, horizontal, vertical, io.KeyShift, io.KeyAlt);
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndPopup();
            }
            return changed;
        }

        /// @brief 位置と大きさの 2 行（アンカーを開いた向きは、親の端からの余白で見せる）
        /// @details 余白は Y が下向き。左 = 位置 − 基準点 × 差、右 = −(位置 + (1 − 基準点) × 差)。
        bool DrawOffsetRows(RectTransformComponent& rect)
        {
            // 編集を始める前の配置を控える
            static const RectTransformComponent* editing = nullptr;
            static UILayout before;

            const UILayout current = rect.GetLayout();
            const bool stretchedX = current.IsStretchedX();
            const bool stretchedY = current.IsStretchedY();

            const auto lowEdge = [](float position, float pivot, float delta) { return position - pivot * delta; };
            const auto highEdge = [](float position, float pivot, float delta) {
                return -(position + (1.0f - pivot) * delta);
            };

            float first[2] = {
                stretchedX ? lowEdge(current.anchoredPos.x, current.pivot.x, current.size.x) : current.anchoredPos.x,
                stretchedY ? lowEdge(current.anchoredPos.y, current.pivot.y, current.size.y) : current.anchoredPos.y,
            };
            float second[2] = {
                stretchedX ? highEdge(current.anchoredPos.x, current.pivot.x, current.size.x) : current.size.x,
                stretchedY ? highEdge(current.anchoredPos.y, current.pivot.y, current.size.y) : current.size.y,
            };

            const char* const firstLabels[2] = { stretchedX ? "左" : "X", stretchedY ? "上" : "Y" };
            const char* const secondLabels[2] = { stretchedX ? "右" : "幅", stretchedY ? "下" : "高" };
            const char* const firstRow = stretchedX && stretchedY ? "余白（左・上）"
                : stretchedX ? "余白（左）・位置 Y" : "位置 X・余白（上）";
            const char* const secondRow = stretchedX && stretchedY ? "余白（右・下）"
                : stretchedX ? "余白（右）・高さ" : "幅・余白（下）";

            bool changed = false;
            InspectorLayout::BeginRow(firstRow, Editor::Theme::kTextDim);
            changed |= InspectorLayout::VectorField("##offsetFirst", first, 2, 1.0f, 0.0f, 0.0f, firstLabels, "%.1f");
            InspectorLayout::BeginRow(secondRow, Editor::Theme::kTextDim);
            changed |= InspectorLayout::VectorField("##offsetSecond", second, 2, 1.0f, 0.0f, 0.0f, secondLabels, "%.1f");

            if (changed) {
                UILayout layout = current;
                if (stretchedX) {
                    layout.size.x = -second[0] - first[0];
                    layout.anchoredPos.x = first[0] + layout.pivot.x * layout.size.x;
                } else {
                    layout.anchoredPos.x = first[0];
                    layout.size.x = second[0];
                }
                if (stretchedY) {
                    layout.size.y = -second[1] - first[1];
                    layout.anchoredPos.y = first[1] + layout.pivot.y * layout.size.y;
                } else {
                    layout.anchoredPos.y = first[1];
                    layout.size.y = second[1];
                }
                if (editing != &rect) {
                    editing = &rect;
                    before = current;
                }
                rect.SetLayout(layout);
            }
            if (editing == &rect && !ImGui::IsAnyItemActive()) {
                editing = nullptr;
                if (!SameLayout(before, rect.GetLayout())) {
                    PushLayoutCommand(rect, before, MakeLabel(rect, "余白"));
                }
            }
            return changed;
        }
    }

    bool Draw(IComponent& component, const InspectorRenderer::DrawContext& context)
    {
        auto& rect = static_cast<RectTransformComponent&>(component);
        const Reflection::TypeDescriptor* const type = component.GetTypeDescriptor();
        void* const instance = component.GetReflectionInstance();
        if (!type || !instance) {
            return false;
        }

        bool changed = DrawPresetRow(rect);
        const UILayout& layout = rect.GetLayout();
        if (layout.IsStretchedX() || layout.IsStretchedY()) {
            changed |= DrawOffsetRows(rect);
        } else {
            changed |= InspectorRenderer::DrawProperties(*type, instance, context, { "anchoredPosition", "size" });
        }
        changed |= InspectorRenderer::DrawProperties(*type, instance, context,
            { "anchorMin", "anchorMax", "pivot", "rotation", "scale", "sortOrder", "parent" });
        return changed;
    }
}

#endif // CORE_EDITOR
