#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Reflection/Reflect.h"

#include <cstdint>

namespace CoreEngine
{
    class RectTransformComponent;

    /// @brief 合わせ方
    enum class UIFitMode : uint8_t
    {
        Unconstrained, ///< 合わせない
        MinSize,       ///< 中身の最小の大きさにする
        PreferredSize, ///< 中身の望む大きさにする
    };

    /// @brief 自分の幅・高さを中身（並べた子、または文字・画像）に合わせるコンポーネント
    /// @details 同じ物に UILayoutGroup があれば並べた子の大きさの和に、無ければ文字・画像の中身の大きさに合わせる。
    ///          親の UILayoutGroup が大きさを決めている向きは、親の決めた大きさを使う。編集中も効く。
    class UIContentSizeFitterComponent final : public IComponent
    {
    public:
        /// @brief 合わせ方の名前（`UIFitMode` の並び）
        static constexpr const char* kFitModeNames[] = { "合わせない", "最小の大きさ", "望む大きさ" };

        UIContentSizeFitterComponent();
        ~UIContentSizeFitterComponent() override;

        const char* GetTypeName() const override { return "UIContentSizeFitter"; }

        REFLECT_BEGIN(UIContentSizeFitterComponent, "UI の大きさを中身に合わせる")
            REFLECT_ENUM_ACCESSOR("horizontalFit", "幅", GetHorizontalFit, SetHorizontalFit, kFitModeNames,
                p.tooltip = "望む大きさ：子を並べた幅（文字なら文字列の幅）にする")
            REFLECT_ENUM_ACCESSOR("verticalFit", "高さ", GetVerticalFit, SetVerticalFit, kFitModeNames,
                p.tooltip = "望む大きさ：子を並べた高さ（文字なら文字列の高さ）にする")
        REFLECT_END()

        /// @brief UI トランスフォームを使う
        bool RequiresComponent(const IComponent& other) const override;

        /// @brief UI トランスフォームを確保する
        void Awake() override;

        UIFitMode GetHorizontalFit() const { return fit_[0]; }
        void SetHorizontalFit(UIFitMode mode) { fit_[0] = mode; }
        UIFitMode GetVerticalFit() const { return fit_[1]; }
        void SetVerticalFit(UIFitMode mode) { fit_[1] = mode; }

        /// @brief 向きごとの合わせ方（0 = 横・1 = 縦）
        UIFitMode GetFit(int axis) const { return fit_[axis]; }

    private:
        UIFitMode fit_[2] = { UIFitMode::Unconstrained, UIFitMode::PreferredSize };
    };
}
