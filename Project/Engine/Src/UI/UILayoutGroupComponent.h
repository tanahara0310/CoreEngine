#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"
#include "UI/UIAnchor.h"

#include <cstdint>

namespace CoreEngine
{
    class RectTransformComponent;

    /// @brief 並べ方
    enum class UILayoutType : uint8_t
    {
        Vertical,   ///< 上から下へ 1 列に並べる
        Horizontal, ///< 左から右へ 1 行に並べる
        Grid,       ///< 同じ大きさのマスに、左から右・上から下へ並べる
    };

    /// @brief 格子の列と行の数の決め方
    enum class UIGridConstraint : uint8_t
    {
        Flexible,     ///< 幅に入るだけ列を作る
        FixedColumns, ///< 列の数を決める
        FixedRows,    ///< 行の数を決める
    };

    /// @brief 子の UI を自分の四角の中に並べるコンポーネント
    /// @details 子（UI トランスフォームの親が自分のもの）を描く順に並べ、位置と大きさを書く。
    ///          並べた子はアンカーを左上の 1 点にする。非アクティブな子は飛ばす。
    ///          並べ直しは毎フレーム測り、値が変わった子だけを書き換える。編集中も効く。
    /// @note 「大きさを決める」を入れた向きは、子の文字・画像の中身の大きさか UILayoutElement の値を使う。
    ///       どちらも無い子はその向きの大きさ 0 から伸びる。
    class UILayoutGroupComponent final : public IComponent
    {
    public:
        /// @brief 並べ方の名前（`UILayoutType` の並び）
        static constexpr const char* kLayoutTypeNames[] = { "縦に並べる", "横に並べる", "格子" };

        /// @brief 列と行の決め方の名前（`UIGridConstraint` の並び）
        static constexpr const char* kGridConstraintNames[] = { "幅に入るだけ", "列の数を決める", "行の数を決める" };

        /// @brief 子の寄せ方の名前（`UIAnchor` の並び）
        static constexpr const char* kAlignmentNames[] = {
            "左上", "上", "右上", "左", "中央", "右", "左下", "下", "右下",
        };

        UILayoutGroupComponent();
        ~UILayoutGroupComponent() override;

        const char* GetTypeName() const override { return "UILayoutGroup"; }

        REFLECT_BEGIN(UILayoutGroupComponent, "UI を並べる")
            REFLECT_ENUM_ACCESSOR("layoutType", "並べ方", GetLayoutType, SetLayoutType, kLayoutTypeNames)
            REFLECT_PROPERTY(padding_, "内側の余白", p.range = Speed(1.0f),
                p.tooltip = "左・上・右・下（px）")
            REFLECT_PROPERTY(spacing_, "間隔", p.range = Speed(1.0f),
                p.tooltip = "隣の子との間（px）",
                p.saveWhen = &Self::IsList, p.visibleWhen = &Self::IsList)
            REFLECT_PROPERTY(gridSpacing_, "マスの間隔", p.range = Speed(1.0f),
                p.tooltip = "横・縦の隣のマスとの間（px）",
                p.saveWhen = &Self::IsGrid, p.visibleWhen = &Self::IsGrid)
            REFLECT_PROPERTY(cellSize_, "マスの大きさ", p.range = Range(0.0f, 4096.0f, 1.0f),
                p.tooltip = "子はすべてこの大きさになる",
                p.saveWhen = &Self::IsGrid, p.visibleWhen = &Self::IsGrid)
            REFLECT_ENUM_ACCESSOR("gridConstraint", "列と行の決め方", GetGridConstraint, SetGridConstraint,
                kGridConstraintNames, p.saveWhen = &Self::IsGrid, p.visibleWhen = &Self::IsGrid)
            REFLECT_ACCESSOR("constraintCount", "列（行）の数", GetConstraintCount, SetConstraintCount,
                p.range = Range(1.0f, 1024.0f, 0.05f),
                p.saveWhen = &Self::UsesConstraintCount, p.visibleWhen = &Self::UsesConstraintCount)
            REFLECT_ENUM_ACCESSOR("childAlignment", "子の寄せ方", GetChildAlignment, SetChildAlignment,
                kAlignmentNames, p.tooltip = "子をすべて並べても余る所を、どちらへ寄せるか")
            REFLECT_PROPERTY(controlChildWidth_, "子の幅を決める",
                p.tooltip = "入れると子の幅を書く。外すと子の今の幅のまま並べる",
                p.saveWhen = &Self::IsList, p.visibleWhen = &Self::IsList)
            REFLECT_PROPERTY(controlChildHeight_, "子の高さを決める",
                p.tooltip = "入れると子の高さを書く。外すと子の今の高さのまま並べる",
                p.saveWhen = &Self::IsList, p.visibleWhen = &Self::IsList)
            REFLECT_PROPERTY(childForceExpandWidth_, "子を横へ広げる",
                p.tooltip = "余った幅を子へ分ける。幅を決めていなければ、子の置き場だけが広がる",
                p.saveWhen = &Self::IsList, p.visibleWhen = &Self::IsList)
            REFLECT_PROPERTY(childForceExpandHeight_, "子を縦へ広げる",
                p.tooltip = "余った高さを子へ分ける。高さを決めていなければ、子の置き場だけが広がる",
                p.saveWhen = &Self::IsList, p.visibleWhen = &Self::IsList)
            REFLECT_PROPERTY(reverseOrder_, "逆の順に並べる",
                p.tooltip = "描く順の後ろの子から並べる")
        REFLECT_END()

        /// @brief UI トランスフォームを使う
        bool RequiresComponent(const IComponent& other) const override;

        /// @brief UI トランスフォームを確保する
        void Awake() override;

        // ===== 値 =====

        UILayoutType GetLayoutType() const { return layoutType_; }
        void SetLayoutType(UILayoutType type) { layoutType_ = type; }

        /// @brief 内側の余白（左・上・右・下。px）
        Vector4 GetPadding() const { return padding_; }
        void SetPadding(const Vector4& padding) { padding_ = padding; }

        /// @brief 縦・横に並べるときの隣との間（px）
        float GetSpacing() const { return spacing_; }
        void SetSpacing(float spacing) { spacing_ = spacing; }

        /// @brief 格子の隣のマスとの間（px）
        Vector2 GetGridSpacing() const { return gridSpacing_; }
        void SetGridSpacing(const Vector2& spacing) { gridSpacing_ = spacing; }

        /// @brief 格子のマスの大きさ（px）
        Vector2 GetCellSize() const { return cellSize_; }
        void SetCellSize(const Vector2& size) { cellSize_ = size; }

        UIGridConstraint GetGridConstraint() const { return gridConstraint_; }
        void SetGridConstraint(UIGridConstraint constraint) { gridConstraint_ = constraint; }

        /// @brief 決める列（行）の数（1 以上）
        int GetConstraintCount() const { return constraintCount_; }
        void SetConstraintCount(int count) { constraintCount_ = count < 1 ? 1 : count; }

        UIAnchor GetChildAlignment() const { return childAlignment_; }
        void SetChildAlignment(UIAnchor alignment) { childAlignment_ = alignment; }

        /// @brief 子の大きさを決める向きか（0 = 横・1 = 縦。格子は常に決める）
        bool ControlsChildSize(int axis) const;
        void SetControlChildSize(int axis, bool control) { (axis == 0 ? controlChildWidth_ : controlChildHeight_) = control; }

        /// @brief 余った大きさを子へ分ける向きか（0 = 横・1 = 縦）
        bool ForcesExpand(int axis) const;
        void SetForceExpand(int axis, bool expand) { (axis == 0 ? childForceExpandWidth_ : childForceExpandHeight_) = expand; }

        bool IsReverseOrder() const { return reverseOrder_; }
        void SetReverseOrder(bool reverse) { reverseOrder_ = reverse; }

        /// @brief 子の寄せ方の割合（0 = 左・上 / 0.5 = 中央 / 1 = 右・下）
        float GetAlignmentRatio(int axis) const;

        // ===== 並べた結果 =====

        /// @brief 並べた最初の子の大きさと間隔の和（0 = 横・1 = 縦。スクロールのホイール 1 目盛りに使う）
        /// @return 並べた子が無ければ 0
        float GetFirstChildStep(int axis) const { return firstChildStep_[axis]; }

        /// @brief 並べた最初の子の大きさと間隔の和を控える
        void SetFirstChildStep(const Vector2& step) { firstChildStep_[0] = step.x; firstChildStep_[1] = step.y; }

    private:
        static bool IsList(const void* o)
        {
            return static_cast<const UILayoutGroupComponent*>(o)->layoutType_ != UILayoutType::Grid;
        }
        static bool IsGrid(const void* o)
        {
            return static_cast<const UILayoutGroupComponent*>(o)->layoutType_ == UILayoutType::Grid;
        }
        static bool UsesConstraintCount(const void* o)
        {
            const auto* self = static_cast<const UILayoutGroupComponent*>(o);
            return self->layoutType_ == UILayoutType::Grid && self->gridConstraint_ != UIGridConstraint::Flexible;
        }

        UILayoutType layoutType_ = UILayoutType::Vertical;
        Vector4 padding_ = { 0.0f, 0.0f, 0.0f, 0.0f };
        float spacing_ = 0.0f;
        Vector2 gridSpacing_ = { 0.0f, 0.0f };
        Vector2 cellSize_ = { 100.0f, 100.0f };
        UIGridConstraint gridConstraint_ = UIGridConstraint::Flexible;
        int constraintCount_ = 2;
        UIAnchor childAlignment_ = UIAnchor::TopLeft;
        bool controlChildWidth_ = true;
        bool controlChildHeight_ = true;
        bool childForceExpandWidth_ = true;
        bool childForceExpandHeight_ = true;
        bool reverseOrder_ = false;

        float firstChildStep_[2] = { 0.0f, 0.0f };
    };
}
