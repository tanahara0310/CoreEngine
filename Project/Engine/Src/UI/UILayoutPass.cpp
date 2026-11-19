#include "pch.h"
#include "UI/UILayoutPass.h"

#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIContentSizeFitterComponent.h"
#include "UI/UIImageComponent.h"
#include "UI/UILayoutElementComponent.h"
#include "UI/UILayoutGroupComponent.h"
#include "UI/UITextComponent.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        /// 並べる部品・大きさを合わせる部品の数
        int gLiveCount = 0;

        /// UI トランスフォームを書き換えたときに呼ぶ関数
        UILayoutPass::RectChanged gRectChanged = nullptr;

        /// @brief 書き換えた後の値が前と違えば、書き換えたことを知らせる
        void NotifyIfChanged(RectTransformComponent& rect, const UILayoutPass::RectState& before)
        {
            if (gRectChanged && !(UILayoutPass::RectState::Of(rect) == before)) {
                gRectChanged(rect, before);
            }
        }

        /// @brief 1 つの向きの大きさの決まり方
        struct AxisSizes
        {
            float min = 0.0f;       ///< 足りないときに縮める下限
            float preferred = 0.0f; ///< 足りていればこの大きさ
            float flexible = 0.0f;  ///< 余りの取り分
        };

        /// @brief UI トランスフォーム 1 つ分
        struct Node
        {
            RectTransformComponent* rect = nullptr;
            UILayoutGroupComponent* group = nullptr;
            UIContentSizeFitterComponent* fitter = nullptr;
            UILayoutElementComponent* element = nullptr;
            UITextComponent* text = nullptr;
            UIImageComponent* image = nullptr;
            int parent = -1;
            /// アクティブな子（描く順）
            std::vector<int> children;
            /// 親が並べない（「並べない」を入れている）
            bool ignored = false;
            AxisSizes sizes[2];
            bool measuring = false;
            bool measured = false;
            bool applied = false;
        };

        template <class T>
        T* FindEnabled(GameObject& owner)
        {
            T* const component = owner.GetComponent<T>();
            return (component && component->IsEnabled()) ? component : nullptr;
        }

        float AxisOf(const Vector2& value, int axis)
        {
            return axis == 0 ? value.x : value.y;
        }

        void SetAxisOf(Vector2& value, int axis, float component)
        {
            (axis == 0 ? value.x : value.y) = component;
        }

        /// @brief 下限を先に見る挟み込み（下限が上限より大きければ下限を優先する）
        float ClampPreferMin(float value, float low, float high)
        {
            if (value < low) {
                return low;
            }
            return value > high ? high : value;
        }

        /// @brief シーンの UI の木を測って並べる
        class LayoutSolver
        {
        public:
            LayoutSolver(std::vector<Node>& nodes, const Vector2& canvasSize)
                : nodes_(nodes), canvasSize_(canvasSize)
            {
            }

            /// @brief 自分と子孫を、親から子の順に合わせて並べる
            void Apply(int index)
            {
                Node& node = nodes_[index];
                if (node.applied) {
                    return;
                }
                node.applied = true;

                const bool arrangedByParent = node.parent >= 0 && nodes_[node.parent].group && !node.ignored;
                if (node.fitter && !arrangedByParent) {
                    Measure(index);
                    ApplyFitter(node);
                }
                if (node.group) {
                    Measure(index);
                    if (node.group->GetLayoutType() == UILayoutType::Grid) {
                        ArrangeGrid(node);
                    } else {
                        ArrangeList(node);
                    }
                }
                for (const int child : node.children) {
                    Apply(child);
                }
            }

        private:
            /// @brief 子から先に、望む大きさを測る
            void Measure(int index)
            {
                Node& node = nodes_[index];
                if (node.measured || node.measuring) {
                    return;
                }
                node.measuring = true;
                for (const int child : node.children) {
                    Measure(child);
                }

                node.sizes[0] = {};
                node.sizes[1] = {};
                if (node.group) {
                    MeasureGroup(node);
                } else {
                    MeasureContent(node);
                }

                for (int axis = 0; axis < 2; ++axis) {
                    AxisSizes& sizes = node.sizes[axis];
                    if (node.element) {
                        if (node.element->GetMinSize(axis) >= 0.0f) {
                            sizes.min = node.element->GetMinSize(axis);
                        }
                        if (node.element->GetPreferredSize(axis) >= 0.0f) {
                            sizes.preferred = node.element->GetPreferredSize(axis);
                        }
                        if (node.element->GetFlexibleSize(axis) >= 0.0f) {
                            sizes.flexible = node.element->GetFlexibleSize(axis);
                        }
                    }
                    sizes.preferred = (std::max)(sizes.preferred, sizes.min);
                }
                node.measuring = false;
                node.measured = true;
            }

            /// @brief 文字は文字列の大きさ、テクスチャを指した画像は 1 コマの大きさを望む
            void MeasureContent(Node& node)
            {
                Vector2 preferred = { 0.0f, 0.0f };
                if (node.text) {
                    preferred = node.text->GetPreferredSize();
                } else if (node.image && node.image->HasTexture()) {
                    preferred = node.image->GetFrameSize();
                }
                node.sizes[0].preferred = preferred.x;
                node.sizes[1].preferred = preferred.y;
            }

            /// @brief 並べる子（並べないものを除き、逆の順ならひっくり返す）
            std::vector<int> ArrangedChildren(const Node& node) const
            {
                std::vector<int> items;
                items.reserve(node.children.size());
                for (const int child : node.children) {
                    if (!nodes_[child].ignored) {
                        items.push_back(child);
                    }
                }
                if (node.group && node.group->IsReverseOrder()) {
                    std::reverse(items.begin(), items.end());
                }
                return items;
            }

            /// @brief 子の今の大きさ（大きさを合わせる部品があれば、合わせた後の大きさ）
            float CurrentSize(int index, int axis) const
            {
                const Node& node = nodes_[index];
                if (node.fitter) {
                    const UIFitMode mode = node.fitter->GetFit(axis);
                    if (mode == UIFitMode::MinSize) {
                        return node.sizes[axis].min;
                    }
                    if (mode == UIFitMode::PreferredSize) {
                        return node.sizes[axis].preferred;
                    }
                }
                return AxisOf(node.rect->GetRectSize(canvasSize_), axis);
            }

            /// @brief 群から見た子 1 つの大きさの決まり方
            AxisSizes ChildSizes(int index, int axis, const UILayoutGroupComponent& group) const
            {
                AxisSizes sizes;
                if (group.ControlsChildSize(axis)) {
                    sizes = nodes_[index].sizes[axis];
                } else {
                    const float current = CurrentSize(index, axis);
                    sizes = { current, current, 0.0f };
                }
                if (group.ForcesExpand(axis)) {
                    sizes.flexible = (std::max)(sizes.flexible, 1.0f);
                }
                return sizes;
            }

            /// @brief 格子の列と行の数
            static void GridCounts(const UILayoutGroupComponent& group, int count, float width,
                                   int& outColumns, int& outRows)
            {
                const Vector2 cell = group.GetCellSize();
                const Vector2 spacing = group.GetGridSpacing();
                const Vector4 padding = group.GetPadding();
                const int constraint = group.GetConstraintCount();
                switch (group.GetGridConstraint()) {
                case UIGridConstraint::FixedColumns:
                    outColumns = constraint;
                    break;
                case UIGridConstraint::FixedRows:
                    outColumns = (count + constraint - 1) / constraint;
                    break;
                case UIGridConstraint::Flexible:
                default: {
                    const float pitch = cell.x + spacing.x;
                    const float inner = width - padding.x - padding.z;
                    outColumns = pitch > 0.0f
                        ? static_cast<int>(std::floor((inner + spacing.x + 0.001f) / pitch)) : count;
                    break;
                }
                }
                outColumns = (std::max)(outColumns, 1);
                outRows = group.GetGridConstraint() == UIGridConstraint::FixedRows
                    ? constraint : (count + outColumns - 1) / outColumns;
            }

            /// @brief 並べた子の大きさの和から、群の大きさを測る
            void MeasureGroup(Node& node)
            {
                const UILayoutGroupComponent& group = *node.group;
                const Vector4 padding = group.GetPadding();
                const float paddingTotal[2] = { padding.x + padding.z, padding.y + padding.w };
                const std::vector<int> items = ArrangedChildren(node);
                const int count = static_cast<int>(items.size());

                if (group.GetLayoutType() == UILayoutType::Grid) {
                    const Vector2 cell = group.GetCellSize();
                    const Vector2 spacing = group.GetGridSpacing();
                    int columns = 0;
                    int rows = 0;
                    if (count > 0) {
                        GridCounts(group, count, AxisOf(node.rect->GetRectSize(canvasSize_), 0), columns, rows);
                    }
                    // 幅は、列の数を決めていなければ正方形に近い並びを望む
                    int preferredColumns = columns;
                    int minColumns = columns;
                    if (count > 0 && group.GetGridConstraint() == UIGridConstraint::Flexible) {
                        preferredColumns = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count))));
                        minColumns = 1;
                    }
                    const auto extent = [](int cells, float cellSize, float gap) {
                        return cells > 0 ? cells * cellSize + (cells - 1) * gap : 0.0f;
                    };
                    node.sizes[0].min = paddingTotal[0] + extent(minColumns, cell.x, spacing.x);
                    node.sizes[0].preferred = paddingTotal[0] + extent(preferredColumns, cell.x, spacing.x);
                    node.sizes[1].min = paddingTotal[1] + extent(rows, cell.y, spacing.y);
                    node.sizes[1].preferred = node.sizes[1].min;
                    return;
                }

                const int mainAxis = group.GetLayoutType() == UILayoutType::Vertical ? 1 : 0;
                const float spacing = group.GetSpacing();
                for (int axis = 0; axis < 2; ++axis) {
                    AxisSizes total{ paddingTotal[axis], paddingTotal[axis], 0.0f };
                    for (const int child : items) {
                        const AxisSizes sizes = ChildSizes(child, axis, group);
                        if (axis == mainAxis) {
                            total.min += sizes.min;
                            total.preferred += sizes.preferred;
                            total.flexible += sizes.flexible;
                        } else {
                            total.min = (std::max)(total.min, paddingTotal[axis] + sizes.min);
                            total.preferred = (std::max)(total.preferred, paddingTotal[axis] + sizes.preferred);
                            total.flexible = (std::max)(total.flexible, sizes.flexible);
                        }
                    }
                    if (axis == mainAxis && count > 1) {
                        total.min += spacing * static_cast<float>(count - 1);
                        total.preferred += spacing * static_cast<float>(count - 1);
                    }
                    node.sizes[axis] = total;
                }
            }

            /// @brief 自分の大きさを中身に合わせる
            void ApplyFitter(Node& node)
            {
                Vector2 size = node.rect->GetRectSize(canvasSize_);
                bool fits = false;
                for (int axis = 0; axis < 2; ++axis) {
                    const UIFitMode mode = node.fitter->GetFit(axis);
                    if (mode == UIFitMode::Unconstrained) {
                        continue;
                    }
                    SetAxisOf(size, axis, mode == UIFitMode::MinSize
                        ? node.sizes[axis].min : node.sizes[axis].preferred);
                    fits = true;
                }
                if (fits) {
                    const UILayoutPass::RectState before = UILayoutPass::RectState::Of(*node.rect);
                    node.rect->SetRectSize(size, canvasSize_);
                    NotifyIfChanged(*node.rect, before);
                }
            }

            /// @brief 子 1 つの、1 つの向きの位置と大きさを書く（親の左上からの距離・px）
            void PlaceChild(int index, int axis, float position, float size)
            {
                RectTransformComponent& rect = *nodes_[index].rect;
                const UILayoutPass::RectState before = UILayoutPass::RectState::Of(rect);
                Vector2 anchorMin = rect.GetAnchorMin();
                Vector2 anchorMax = rect.GetAnchorMax();
                SetAxisOf(anchorMin, axis, 0.0f);
                SetAxisOf(anchorMax, axis, 0.0f);
                rect.SetAnchors(anchorMin, anchorMax);

                Vector2 rectSize = rect.GetSize();
                SetAxisOf(rectSize, axis, size);
                rect.SetSize(rectSize);

                Vector2 anchored = rect.GetAnchoredPosition();
                const float pivoted = position + size * AxisOf(rect.GetPivot(), axis);
                if (AxisOf(anchored, axis) != pivoted) {
                    SetAxisOf(anchored, axis, pivoted);
                    rect.SetAnchoredPosition(anchored);
                }
                NotifyIfChanged(rect, before);
            }

            /// @brief 縦か横に 1 列で並べる
            void ArrangeList(Node& node)
            {
                const UILayoutGroupComponent& group = *node.group;
                const std::vector<int> items = ArrangedChildren(node);
                const Vector2 size = node.rect->GetRectSize(canvasSize_);
                const Vector4 padding = group.GetPadding();
                const float paddingStart[2] = { padding.x, padding.y };
                const float paddingTotal[2] = { padding.x + padding.z, padding.y + padding.w };
                const int mainAxis = group.GetLayoutType() == UILayoutType::Vertical ? 1 : 0;
                const float spacing = group.GetSpacing();

                for (int axis = 0; axis < 2; ++axis) {
                    const float extent = AxisOf(size, axis);
                    const float inner = extent - paddingTotal[axis];
                    const float alignment = group.GetAlignmentRatio(axis);
                    const bool controls = group.ControlsChildSize(axis);

                    if (axis != mainAxis) {
                        for (const int child : items) {
                            const AxisSizes sizes = ChildSizes(child, axis, group);
                            const float childSize = controls
                                ? ClampPreferMin(inner, sizes.min, sizes.flexible > 0.0f ? inner : sizes.preferred)
                                : sizes.preferred;
                            PlaceChild(child, axis, paddingStart[axis] + (inner - childSize) * alignment, childSize);
                        }
                        continue;
                    }

                    float totalMin = paddingTotal[axis];
                    float totalPreferred = paddingTotal[axis];
                    float totalFlexible = 0.0f;
                    for (const int child : items) {
                        const AxisSizes sizes = ChildSizes(child, axis, group);
                        totalMin += sizes.min;
                        totalPreferred += sizes.preferred;
                        totalFlexible += sizes.flexible;
                    }
                    if (items.size() > 1) {
                        const float gaps = spacing * static_cast<float>(items.size() - 1);
                        totalMin += gaps;
                        totalPreferred += gaps;
                    }

                    // 望む大きさで並べて余れば取り分で分け、取り分が無ければ寄せる。
                    // 足りなければ最小と望む大きさの間で縮める
                    float position = paddingStart[axis];
                    float flexibleUnit = 0.0f;
                    const float surplus = extent - totalPreferred;
                    if (surplus > 0.0f) {
                        if (totalFlexible == 0.0f) {
                            position += (inner - (totalPreferred - paddingTotal[axis])) * alignment;
                        } else {
                            flexibleUnit = surplus / totalFlexible;
                        }
                    }
                    float shrink = 0.0f;
                    if (totalMin != totalPreferred) {
                        shrink = std::clamp((extent - totalMin) / (totalPreferred - totalMin), 0.0f, 1.0f);
                    }

                    for (const int child : items) {
                        const AxisSizes sizes = ChildSizes(child, axis, group);
                        const float slot = sizes.min + (sizes.preferred - sizes.min) * shrink
                                         + sizes.flexible * flexibleUnit;
                        if (controls) {
                            PlaceChild(child, axis, position, slot);
                        } else {
                            // 大きさを決めない子は、置き場の中で寄せる
                            PlaceChild(child, axis, position + (slot - sizes.preferred) * alignment, sizes.preferred);
                        }
                        position += slot + spacing;
                    }
                }

                Vector2 step = { 0.0f, 0.0f };
                if (!items.empty()) {
                    SetAxisOf(step, mainAxis, AxisOf(nodes_[items.front()].rect->GetRectSize(canvasSize_), mainAxis) + spacing);
                }
                node.group->SetFirstChildStep(step);
            }

            /// @brief 同じ大きさのマスに、左から右・上から下へ並べる
            void ArrangeGrid(Node& node)
            {
                const UILayoutGroupComponent& group = *node.group;
                const std::vector<int> items = ArrangedChildren(node);
                const Vector2 size = node.rect->GetRectSize(canvasSize_);
                const Vector4 padding = group.GetPadding();
                const Vector2 cell = group.GetCellSize();
                const Vector2 spacing = group.GetGridSpacing();
                const int count = static_cast<int>(items.size());

                node.group->SetFirstChildStep(count > 0 ? Vector2{ cell.x + spacing.x, cell.y + spacing.y }
                                                        : Vector2{ 0.0f, 0.0f });
                if (count == 0) {
                    return;
                }

                int columns = 1;
                int rows = 1;
                GridCounts(group, count, size.x, columns, rows);
                const int usedColumns = std::clamp(columns, 1, count);
                const int usedRows = std::clamp((count + columns - 1) / columns, 1, (std::max)(rows, 1));

                const Vector2 required = {
                    usedColumns * cell.x + (usedColumns - 1) * spacing.x,
                    usedRows * cell.y + (usedRows - 1) * spacing.y,
                };
                const Vector2 start = {
                    padding.x + (size.x - padding.x - padding.z - required.x) * group.GetAlignmentRatio(0),
                    padding.y + (size.y - padding.y - padding.w - required.y) * group.GetAlignmentRatio(1),
                };

                for (int i = 0; i < count; ++i) {
                    const int column = i % columns;
                    const int row = i / columns;
                    PlaceChild(items[i], 0, start.x + (cell.x + spacing.x) * column, cell.x);
                    PlaceChild(items[i], 1, start.y + (cell.y + spacing.y) * row, cell.y);
                }
            }

            std::vector<Node>& nodes_;
            Vector2 canvasSize_;
        };
    }

    namespace UILayoutPass
    {
        RectState RectState::Of(const RectTransformComponent& rect)
        {
            return { rect.GetAnchorMin(), rect.GetAnchorMax(), rect.GetSize(), rect.GetAnchoredPosition() };
        }

        void RectState::ApplyTo(RectTransformComponent& rect) const
        {
            rect.SetAnchors(anchorMin, anchorMax);
            rect.SetSize(size);
            rect.SetAnchoredPosition(anchoredPosition);
        }

        void SetRectChangedCallback(RectChanged callback)
        {
            gRectChanged = callback;
        }

        void NotifyCreated()
        {
            ++gLiveCount;
        }

        void NotifyDestroyed()
        {
            --gLiveCount;
        }

        bool AnyAlive()
        {
            return gLiveCount > 0;
        }
    }

    void ResolveUILayout(GameObjectManager& manager)
    {
        if (!UILayoutPass::AnyAlive()) {
            return;
        }

        std::vector<Node> nodes;
        std::unordered_map<const RectTransformComponent*, int> indexOf;
        bool anyLayout = false;
        manager.ForEachComponent<RectTransformComponent>([&](RectTransformComponent& rect, GameObject& owner) {
            Node node;
            node.rect = &rect;
            node.group = FindEnabled<UILayoutGroupComponent>(owner);
            node.fitter = FindEnabled<UIContentSizeFitterComponent>(owner);
            node.element = FindEnabled<UILayoutElementComponent>(owner);
            node.ignored = node.element && node.element->IsIgnoreLayout();
            anyLayout = anyLayout || node.group || node.fitter;
            indexOf.emplace(&rect, static_cast<int>(nodes.size()));
            nodes.push_back(node);
            });
        if (!anyLayout) {
            return;
        }

        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            Node& node = nodes[i];
            GameObject* const owner = node.rect->GetOwner();
            node.text = owner ? FindEnabled<UITextComponent>(*owner) : nullptr;
            node.image = owner ? FindEnabled<UIImageComponent>(*owner) : nullptr;
            const RectTransformComponent* const parent = node.rect->GetParent();
            const auto found = parent ? indexOf.find(parent) : indexOf.end();
            if (found != indexOf.end()) {
                node.parent = found->second;
                nodes[found->second].children.push_back(i);
            }
        }

        // 子は描く順（描画順、同じなら並びの順）に並べる
        for (Node& node : nodes) {
            std::stable_sort(node.children.begin(), node.children.end(), [&](int a, int b) {
                return nodes[a].rect->GetSortOrder() < nodes[b].rect->GetSortOrder();
                });
        }

        LayoutSolver solver(nodes, nodes.front().rect->FindCanvasSize());
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            if (nodes[i].parent < 0) {
                solver.Apply(i);
            }
        }
        // 親をたどると輪になっている物は、輪の中で最初の物から合わせる
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            solver.Apply(i);
        }
    }
}
