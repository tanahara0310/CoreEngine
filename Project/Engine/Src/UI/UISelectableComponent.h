#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector4.h"
#include "UI/IUIInteractable.h"

#include <cstdint>

namespace CoreEngine
{
    class RectTransformComponent;

    /// @brief ポインタとキー・パッドで操作する UI（ボタン・トグル・スライダー）の土台
    /// @details 乗っている・選んでいる・押しているの状態と状態ごとの 4 色を持ち、状態が変わるたびに見た目を作り直す。
    class UISelectableComponent : public IComponent, public IUIInteractable
    {
    public:
        /// @brief 同じオブジェクトに `RectTransform` が無ければ足し、見た目を合わせる
        void Awake() override;

        /// @brief プロパティが変わったら見た目を作り直す
        void OnPropertyChanged(const Reflection::PropertyDescriptor& property) override;

        /// @brief `RectTransform` を控えるので、外せないようにする
        bool RequiresComponent(const IComponent& other) const override;

        // ===== 問い合わせ =====

        /// @brief ポインタが乗っているか
        bool IsHovered() const { return hovered_; }

        /// @brief キー・パッドで選ばれているか
        bool IsFocused() const { return focused_; }

        /// @brief 押し下げられているか
        bool IsPressed() const { return pressed_; }

        /// @brief 押せるか
        bool IsInteractable() const { return interactable_; }

        /// @brief 押せるかを変える（押せなくしたら、乗っている・選んでいる・押している状態を消す）
        void SetInteractable(bool value);

        // ===== IUIInteractable =====

        RectTransformComponent* GetRectTransform() const override;
        int GetPointerSortOrder() const override;

        void OnPointerEnter() override;
        void OnPointerExit() override;
        void OnFocusEnter() override;
        void OnFocusExit() override;
        void OnPressBegin() override;

        /// @brief 押し下げを解き、自分の上で離して押せるなら `OnClick()` を呼んでから見た目を作り直す
        void OnPressEnd(bool onSelf) override;

    protected:
        /// @brief 状態ごとの色の既定値を受け取る
        UISelectableComponent(const Vector4& normalColor, const Vector4& hoveredColor,
                              const Vector4& pressedColor, const Vector4& disabledColor);

        /// @brief 自分の上で押して、自分の上で離した（押せるときだけ呼ばれる）
        virtual void OnClick() {}

        /// @brief 今の状態の色を同じオブジェクトの `UIImage` へ書く
        virtual void ApplyVisual();

        /// @brief 今の状態に応じた色
        const Vector4& CurrentColor() const;

        /// @brief 今が、記録したフレームの次のフレームか
        /// @param recordedFrame 記録したフレーム（0 は一度も記録していない）
        static bool IsFrameAfter(std::uint64_t recordedFrame);

        Vector4 normalColor_;
        Vector4 hoveredColor_;
        Vector4 pressedColor_;
        Vector4 disabledColor_;

    private:
        bool interactable_ = true;

        bool hovered_ = false;
        bool focused_ = false;
        bool pressed_ = false;
    };
}
