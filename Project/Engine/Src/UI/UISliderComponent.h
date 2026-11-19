#pragma once

#include "GameObject/Component/Core/ObjectRef.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"
#include "UI/UISelectableComponent.h"

#include <cstdint>

namespace CoreEngine
{
    class RectTransformComponent;

    /// @brief つまみを動かして値を決める帯
    /// @details 当たりは同じオブジェクトの `RectTransform` で取る。
    ///          「伸びる帯」と「つまみ」に別のオブジェクトを繋ぐと、値に合わせて
    ///          大きさと位置を合わせる。繋がなくても値は動く。
    /// @note ポインタは帯のどこを押しても、その位置の値になる。
    ///       フォーカス中は向きの入力で動き、その向きのフォーカス送りは起きない。
    /// @warning 繋いだ相手のアンカーは帯と同じものに揃える（同じ座標系で置くため）。
    class UISliderComponent final : public UISelectableComponent
    {
    public:
        /// @brief 値が増える向き
        enum class Direction : uint8_t
        {
            LeftToRight,  ///< 左から右
            RightToLeft,  ///< 右から左
            TopToBottom,  ///< 上から下
            BottomToTop,  ///< 下から上
        };

        /// @brief 向きの名前（`Direction` の並び）
        static constexpr const char* kDirectionNames[] = {
            "左から右", "右から左", "上から下", "下から上",
        };

        UISliderComponent();

        const char* GetTypeName() const override { return "UISlider"; }

        REFLECT_BEGIN(UISliderComponent, "UI スライダー")
            REFLECT_ACCESSOR("interactable", "動かせる", IsInteractable, SetInteractable,
                p.tooltip = "外すと動かせない色になり、ポインタもフォーカスも受け付けなくなる")
            REFLECT_ENUM_ACCESSOR("direction", "向き", GetDirection, SetDirection, kDirectionNames)
            REFLECT_ACCESSOR("minValue", "最小値", GetMinValue, SetMinValue, p.range = Speed(0.1f))
            REFLECT_ACCESSOR("maxValue", "最大値", GetMaxValue, SetMaxValue, p.range = Speed(0.1f))
            REFLECT_ACCESSOR("value", "値", GetValue, SetValue, p.range = Speed(0.01f))
            REFLECT_ACCESSOR("wholeNumbers", "整数だけ", IsWholeNumbers, SetWholeNumbers,
                p.tooltip = "入れると値を整数に丸める")
            REFLECT_PROPERTY(navigationStep_, "キーで動く量", p.range = Speed(0.01f),
                p.tooltip = "0 なら（最大値 − 最小値）の 1/10 ずつ動く")
            REFLECT_OBJECT_REF(fill_, "伸びる帯")
            REFLECT_OBJECT_REF(handle_, "つまみ")
            REFLECT_PROPERTY(normalColor_, "通常の色",
                p.type = ::CoreEngine::Reflection::PropertyType::Color)
            REFLECT_PROPERTY(hoveredColor_, "乗せたときの色",
                p.type = ::CoreEngine::Reflection::PropertyType::Color,
                p.tooltip = "キー・パッドで選んでいるときもこの色になる")
            REFLECT_PROPERTY(pressedColor_, "掴んでいるときの色",
                p.type = ::CoreEngine::Reflection::PropertyType::Color)
            REFLECT_PROPERTY(disabledColor_, "動かせないときの色",
                p.type = ::CoreEngine::Reflection::PropertyType::Color)
        REFLECT_END()

        /// @brief 同じオブジェクトに `RectTransform` が無ければ足し、見た目と伸びる帯・つまみを合わせる
        void Awake() override;

        /// @brief 値に合わせて伸びる帯とつまみを置く
        void LateUpdate() override;

        /// @brief 値や色が変わったら見た目を作り直し、伸びる帯とつまみを置き直す
        void OnPropertyChanged(const Reflection::PropertyDescriptor& property) override;

        // ===== 問い合わせ =====

        /// @brief 値が変わったか
        /// @details 決まったのは前のフレームの後ろ半分なので、読む側は次のフレームの前半で見る。
        ///          そのフレームだけ true を返す。
        bool WasChanged() const;

        // ===== 値 =====

        float GetValue() const { return value_; }
        void SetValue(float value);

        /// @brief 0〜1 で表した値（最小値で 0、最大値で 1）
        float GetNormalizedValue() const;
        void SetNormalizedValue(float normalized);

        float GetMinValue() const { return minValue_; }
        void SetMinValue(float value);
        float GetMaxValue() const { return maxValue_; }
        void SetMaxValue(float value);

        bool IsWholeNumbers() const { return wholeNumbers_; }
        void SetWholeNumbers(bool wholeNumbers);

        Direction GetDirection() const { return direction_; }
        void SetDirection(Direction direction);

        /// @brief 値に合わせて置く「伸びる帯」と「つまみ」を繋ぐ（nullptr で外す）
        void SetFill(RectTransformComponent* fill);
        void SetHandle(RectTransformComponent* handle);

        // ===== IUIInteractable =====

        bool AcceptsInput() const override { return IsInteractable() && IsEnabled(); }
        void OnDrag(const Vector2& pointerOnCanvas, const Vector2& canvasSize) override;
        bool CapturesDrag() const override { return true; }
        bool OnNavigate(UINavigationDirection direction) override;

    private:
        /// @brief 値の幅（0 で割らないようにした最大値 − 最小値）
        float Span() const;

        /// @brief 帯の中のポインタの位置から値を決める
        void ApplyPointer(const Vector2& pointerOnCanvas, const Vector2& canvasSize);

        /// @brief つまみ（繋いでいなければ帯そのもの）の画像へ色を写す
        void ApplyVisual() override;

        /// @brief 値に合わせて伸びる帯とつまみを置く
        void ApplyLayout();

        /// @brief 前に置いたときから何も変わっていないか
        bool LayoutIsUpToDate(const Vector2& trackPosition, const Vector2& trackSize,
                              float normalized) const;

        /// @brief 値が増える向きが横か
        bool IsHorizontal() const
        {
            return direction_ == Direction::LeftToRight || direction_ == Direction::RightToLeft;
        }

        /// @brief 値が増える向きが、座標の増える向きと逆か
        bool IsReversed() const
        {
            return direction_ == Direction::RightToLeft || direction_ == Direction::BottomToTop;
        }

        float minValue_ = 0.0f;
        float maxValue_ = 1.0f;
        float value_ = 1.0f;
        bool wholeNumbers_ = false;
        float navigationStep_ = 0.0f;
        Direction direction_ = Direction::LeftToRight;

        ObjectRef<RectTransformComponent> fill_;
        ObjectRef<RectTransformComponent> handle_;

        /// 値が変わったフレーム（0 は一度も変わっていない）
        uint64_t changedFrame_ = 0;

        // 前に置いたときの帯と値（変わっていなければ置き直さない）
        Vector2 laidOutPosition_{};
        Vector2 laidOutSize_{};
        float laidOutNormalized_ = -1.0f;
    };
}
