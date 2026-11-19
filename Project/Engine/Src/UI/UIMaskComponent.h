#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
    class RectTransformComponent;

    /// @brief 子の UI を自分の四角の中だけに描くコンポーネント
    /// @details 子と孫の UI は、この物の UI トランスフォームの四角（回転しているなら囲む四角）からはみ出た所を描かず、
    ///          はみ出た所ではポインタも当たらない。マスクの中にマスクがあれば、重なった所だけを描く。
    class UIMaskComponent final : public IComponent
    {
    public:
        const char* GetTypeName() const override { return "UIMask"; }

        REFLECT_BEGIN(UIMaskComponent, "UI マスク")
            REFLECT_ACCESSOR("showMaskGraphic", "自分の画像を描く", IsShowMaskGraphic, SetShowMaskGraphic,
                p.tooltip = "外すと、同じ物の UI 画像を描かずに切る範囲としてだけ使う")
        REFLECT_END()

        /// @brief UI トランスフォームを使う
        bool RequiresComponent(const IComponent& other) const override;

        /// @brief UI トランスフォームを確保する
        void Awake() override;

        /// @brief 同じ物の UI 画像を描くか
        bool IsShowMaskGraphic() const { return showMaskGraphic_; }
        void SetShowMaskGraphic(bool show) { showMaskGraphic_ = show; }

        /// @brief 回転していて囲む四角で切っていることを、Console へ 1 回だけ知らせる
        void WarnRotatedOnce() const;

    private:
        bool showMaskGraphic_ = true;
        mutable bool rotatedWarned_ = false;
    };
}
