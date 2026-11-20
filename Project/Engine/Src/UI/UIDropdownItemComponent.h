#pragma once

#include "GameObject/Component/Core/ObjectRef.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"
#include "UI/UISelectableComponent.h"

namespace CoreEngine
{
    class RectTransformComponent;
    class UIDropdownComponent;

    /// @brief ドロップダウンの一覧の 1 行
    /// @details 一覧の行のひな形に付けておく。ドロップダウンは開くときに、選択肢ごとにひな形を複製して
    ///          何番目の行かを教える。押されると、その番号をドロップダウンへ伝える。
    /// @note 「選んでいる行に出す印」を繋ぐと、今の値の行だけで出す。
    class UIDropdownItemComponent final : public UISelectableComponent
    {
    public:
        UIDropdownItemComponent();

        const char* GetTypeName() const override { return "UIDropdownItem"; }

        REFLECT_DECLARE(UIDropdownItemComponent)

        // ===== 繋ぎ =====

        /// @brief どのドロップダウンの何番目の行かを決める（ドロップダウンが複製した行に呼ぶ）
        void Bind(UIDropdownComponent* dropdown, int index);

        /// @brief 繋いでいるドロップダウン（ひな形なら nullptr）
        UIDropdownComponent* GetDropdown() const;

        /// @brief 何番目の選択肢か（ひな形なら -1）
        int GetIndex() const { return index_; }

        /// @brief 今の値の行かを変え、印を出し入れする
        void SetSelected(bool selected);
        bool IsSelected() const { return selected_; }

        /// @brief 選んでいる行に出す印（オブジェクトごと出し入れする）
        void SetCheckmark(RectTransformComponent* checkmark);

        // ===== IUIInteractable =====

        bool AcceptsInput() const override { return IsInteractable() && IsEnabled(); }

    private:
        /// @brief 自分の番号をドロップダウンへ伝える
        void OnClick() override;

        /// @brief 色を写し、印を出し入れする
        void ApplyVisual() override;

        ObjectRef<RectTransformComponent> checkmark_;

        ObjectRef<UIDropdownComponent> dropdown_;
        int index_ = -1;
        bool selected_ = false;
    };
}
