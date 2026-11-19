#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
    class RectTransformComponent;

    /// @brief 並べられるときの大きさ（最小・望む大きさ・余りの取り分）を決めるコンポーネント
    /// @details 親の UILayoutGroup が子の大きさを決めるときと、UIContentSizeFitter が測るときに使う。
    ///          負の値の欄は使わず、中身（文字・画像）から決まる値のままにする。
    class UILayoutElementComponent final : public IComponent
    {
    public:
        /// @brief 欄を使わない印
        static constexpr float kUnset = -1.0f;

        const char* GetTypeName() const override { return "UILayoutElement"; }

        REFLECT_BEGIN(UILayoutElementComponent, "UI の並べられ方")
            REFLECT_PROPERTY(ignoreLayout_, "並べない",
                p.tooltip = "入れると親の UI を並べる部品はこの物を飛ばし、位置も大きさも書かない")
            REFLECT_PROPERTY(minWidth_, "最小の幅", p.range = Range(-1.0f, 16384.0f, 1.0f),
                p.tooltip = "並べる幅が足りないとき、ここまでは縮む（負の値で使わない）")
            REFLECT_PROPERTY(minHeight_, "最小の高さ", p.range = Range(-1.0f, 16384.0f, 1.0f),
                p.tooltip = "並べる高さが足りないとき、ここまでは縮む（負の値で使わない）")
            REFLECT_PROPERTY(preferredWidth_, "望む幅", p.range = Range(-1.0f, 16384.0f, 1.0f),
                p.tooltip = "幅が足りていればこの幅にする（負の値で使わない）")
            REFLECT_PROPERTY(preferredHeight_, "望む高さ", p.range = Range(-1.0f, 16384.0f, 1.0f),
                p.tooltip = "高さが足りていればこの高さにする（負の値で使わない）")
            REFLECT_PROPERTY(flexibleWidth_, "余った幅の取り分", p.range = Range(-1.0f, 1024.0f, 0.05f),
                p.tooltip = "余った幅を、兄弟の取り分との割合で分けてもらう（負の値で使わない）")
            REFLECT_PROPERTY(flexibleHeight_, "余った高さの取り分", p.range = Range(-1.0f, 1024.0f, 0.05f),
                p.tooltip = "余った高さを、兄弟の取り分との割合で分けてもらう（負の値で使わない）")
        REFLECT_END()

        /// @brief UI トランスフォームを使う
        bool RequiresComponent(const IComponent& other) const override;

        /// @brief UI トランスフォームを確保する
        void Awake() override;

        bool IsIgnoreLayout() const { return ignoreLayout_; }
        void SetIgnoreLayout(bool ignore) { ignoreLayout_ = ignore; }

        /// @brief 最小の大きさ（0 = 横・1 = 縦。負なら使わない）
        float GetMinSize(int axis) const { return axis == 0 ? minWidth_ : minHeight_; }

        /// @brief 望む大きさ（0 = 横・1 = 縦。負なら使わない）
        float GetPreferredSize(int axis) const { return axis == 0 ? preferredWidth_ : preferredHeight_; }

        /// @brief 余りの取り分（0 = 横・1 = 縦。負なら使わない）
        float GetFlexibleSize(int axis) const { return axis == 0 ? flexibleWidth_ : flexibleHeight_; }

    private:
        bool ignoreLayout_ = false;
        float minWidth_ = kUnset;
        float minHeight_ = kUnset;
        float preferredWidth_ = kUnset;
        float preferredHeight_ = kUnset;
        float flexibleWidth_ = kUnset;
        float flexibleHeight_ = kUnset;
    };
}
