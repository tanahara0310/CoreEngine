#pragma once

#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"
#include "UI/UISelectableComponent.h"

#include <cstdint>

namespace CoreEngine
{
    /// @brief 押せるボタン
    /// @details 同じオブジェクトの `UIImage` の色を、通常・乗せた・押した・無効で入れ替える。
    ///          押されたことは `WasClicked()` で問い合わせる（スクリプトは `Update()` で見る）。
    /// @note 当たりを取るのは同じオブジェクトの `RectTransform`。
    ///       クリックが立つのは「自分の上で押して、自分の上で離した」ときだけ。
    ///       ポインタでも、キー・パッドのフォーカス＋決定でも同じように押せる。
    class UIButtonComponent final : public UISelectableComponent
    {
    public:
        UIButtonComponent();

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

        // ===== 問い合わせ =====

        /// @brief 押されたか
        /// @details クリックが決まるのはオブジェクトの更新より後なので、スクリプトが
        ///          `Update()` で見るのは次のフレームになる。そのフレームだけ true を返す。
        /// @note フレーム番号で決めるので、コンポーネントの更新順に左右されない。
        bool WasClicked() const;

        // ===== IUIInteractable =====

        bool AcceptsInput() const override { return IsInteractable(); }

    private:
        /// @brief クリックが決まったフレームを記録する
        void OnClick() override;

        /// クリックが決まったフレーム（0 = まだ一度も押されていない）
        std::uint64_t clickedFrame_ = 0;
    };
}
