#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Core/ObjectRef.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"
#include "UI/IUIInteractable.h"

#include <cstdint>

namespace CoreEngine
{
    class RectTransformComponent;
    class UIImageComponent;

    /// @brief 入りと切りを覚えて、押すたびに入れ替える箱
    /// @details 押されたことしか返さないボタンと違い、今どちらなのかを持つ。
    ///          「入りのときに出す印」に画像を繋ぐと、入りのときだけ出す。
    /// @note 同じ `グループ` を付けたトグル同士は、1 つだけが入りになる
    ///       （どれか 1 つを選ぶ画面に使う）。空なら他と関わらない。
    class UIToggleComponent final : public IComponent, public IUIInteractable
    {
    public:
        const char* GetTypeName() const override { return "UIToggle"; }

        REFLECT_BEGIN(UIToggleComponent, "UI トグル")
            REFLECT_ACCESSOR("isOn", "入り", IsOn, SetIsOn)
            REFLECT_ACCESSOR("interactable", "押せる", IsInteractable, SetInteractable,
                p.tooltip = "外すと押せない色になり、ポインタもフォーカスも受け付けなくなる")
            REFLECT_OBJECT_REF(checkmark_, "入りのときに出す印")
            REFLECT_PROPERTY(group_, "グループ",
                p.tooltip = "同じ綴りのトグル同士で 1 つだけが入りになる。空なら他と関わらない")
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

        /// @brief 同じオブジェクトに `RectTransform` が無ければ足し、印の出し入れを合わせる
        void Awake() override;

        /// @brief 値や色が変わったら見た目を作り直す
        void OnPropertyChanged(const Reflection::PropertyDescriptor& property) override;

        /// @brief `RectTransform` を控えるので、外せないようにする
        bool RequiresComponent(const IComponent& other) const override;

        // ===== 問い合わせ =====

        /// @brief 入り切りが変わったか
        /// @details 決まったのは前のフレームの後ろ半分なので、読む側は次のフレームの前半で見る。
        ///          そのフレームだけ true を返す。
        bool WasChanged() const;

        // ===== 値 =====

        bool IsOn() const { return isOn_; }

        /// @brief 入り切りを設定する（グループを付けていれば、入りにしたとき仲間を切る）
        void SetIsOn(bool isOn);

        const std::string& GetGroup() const { return group_; }
        void SetGroup(const std::string& group) { group_ = group; }

        bool IsInteractable() const { return interactable_; }
        void SetInteractable(bool value);

        // ===== IUIInteractable =====

        RectTransformComponent* GetRectTransform() const override;
        bool AcceptsInput() const override { return interactable_ && IsEnabled(); }
        int GetPointerSortOrder() const override;

        void OnPointerEnter() override;
        void OnPointerExit() override;
        void OnFocusEnter() override;
        void OnFocusExit() override;
        void OnPressBegin() override;
        void OnPressEnd(bool onSelf) override;

    private:
        /// @brief 今の状態で使う色
        const Vector4& CurrentColor() const;

        /// @brief 色を写し、印を出し入れする
        void ApplyVisual();

        /// @brief 同じグループの仲間を切る
        void TurnOffGroupSiblings();

        bool isOn_ = false;
        bool interactable_ = true;
        std::string group_;

        ObjectRef<UIImageComponent> checkmark_;

        Vector4 normalColor_   = { 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 hoveredColor_  = { 0.85f, 0.9f, 1.0f, 1.0f };
        Vector4 pressedColor_  = { 0.7f, 0.78f, 0.95f, 1.0f };
        Vector4 disabledColor_ = { 0.6f, 0.6f, 0.6f, 0.5f };

        bool hovered_ = false;
        bool focused_ = false;
        bool pressed_ = false;

        /// 入り切りが変わったフレーム（0 は一度も変わっていない）
        uint64_t changedFrame_ = 0;
    };
}
