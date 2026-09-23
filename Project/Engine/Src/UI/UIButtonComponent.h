#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"
#include "UI/IUIInteractable.h"

#include <cstdint>

namespace CoreEngine
{
    class RectTransformComponent;
    class UIImageComponent;

    /// @brief 押せるボタン
    /// @details 同じオブジェクトの `UIImage` の色を、通常・乗せた・押した・無効で入れ替える。
    ///          押されたことは `WasClicked()` で問い合わせる（スクリプトは `Update()` で見る）。
    /// @note 当たりを取るのは同じオブジェクトの `RectTransform`。
    ///       クリックが立つのは「自分の上で押して、自分の上で離した」ときだけ。
    ///       ポインタでも、キー・パッドのフォーカス＋決定でも同じように押せる。
    class UIButtonComponent final : public IComponent, public IUIInteractable
    {
    public:
        const char* GetTypeName() const override { return "UIButton"; }

        REFLECT_BEGIN(UIButtonComponent, "UI ボタン")
            REFLECT_ACCESSOR("interactable", "押せる", IsInteractable, SetInteractable,
                p.tooltip = "外すと押せない色になり、ポインタを受け付けなくなる")
            REFLECT_PROPERTY(normalColor_, "通常の色",
                p.type = ::CoreEngine::Reflection::PropertyType::Color)
            REFLECT_PROPERTY(hoveredColor_, "乗せたときの色",
                p.type = ::CoreEngine::Reflection::PropertyType::Color,
                p.tooltip = "キー・パッドで選んでいるときもこの色になる")
            REFLECT_PROPERTY(pressedColor_, "押したときの色",
                p.type = ::CoreEngine::Reflection::PropertyType::Color)
            REFLECT_PROPERTY(disabledColor_, "押せないときの色",
                p.type = ::CoreEngine::Reflection::PropertyType::Color)
        REFLECT_END()

        /// @brief 同じオブジェクトに `RectTransform` が無ければ足し、色を合わせる
        void Awake() override;

        /// @brief 色や押せるかが変わったら見た目を作り直す
        void OnPropertyChanged(const Reflection::PropertyDescriptor& property) override;

        /// @brief `RectTransform` を控えるので、外せないようにする
        bool RequiresComponent(const IComponent& other) const override;

        // ===== 問い合わせ =====

        /// @brief 押されたか
        /// @details クリックが決まるのはオブジェクトの更新より後なので、スクリプトが
        ///          `Update()` で見るのは次のフレームになる。そのフレームだけ true を返す。
        /// @note フレーム番号で決めるので、コンポーネントの更新順に左右されない。
        bool WasClicked() const;

        /// @brief ポインタが乗っているか
        bool IsHovered() const { return hovered_; }

        /// @brief キー・パッドで選ばれているか
        bool IsFocused() const { return focused_; }

        /// @brief 押し下げられているか
        bool IsPressed() const { return pressed_; }

        /// @brief 押せるか
        bool IsInteractable() const { return interactable_; }
        void SetInteractable(bool value);

        // ===== IUIInteractable =====

        RectTransformComponent* GetRectTransform() const override;
        bool AcceptsInput() const override { return interactable_; }
        int GetPointerSortOrder() const override;

        void OnPointerEnter() override;
        void OnPointerExit() override;
        void OnFocusEnter() override;
        void OnFocusExit() override;
        void OnPressBegin() override;
        void OnPressEnd(bool onSelf) override;

    private:
        /// @brief 今の状態の色を `UIImage` へ書く
        void ApplyVisual();

        /// @brief 今の状態に応じた色
        const Vector4& CurrentColor() const;

        bool interactable_ = true;

        Vector4 normalColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 hoveredColor_{ 0.85f, 0.90f, 1.0f, 1.0f };
        Vector4 pressedColor_{ 0.65f, 0.72f, 0.85f, 1.0f };
        Vector4 disabledColor_{ 0.45f, 0.45f, 0.45f, 0.6f };

        bool hovered_ = false;
        bool focused_ = false;
        bool pressed_ = false;

        /// クリックが決まったフレーム（0 = まだ一度も押されていない）
        std::uint64_t clickedFrame_ = 0;
    };
}
