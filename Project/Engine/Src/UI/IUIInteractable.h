#pragma once

namespace CoreEngine
{
    class RectTransformComponent;

    /// @brief ポインタで押せる UI が実装する口
    /// @details `UIInteractionFeature` が毎フレーム、いちばん手前の 1 つだけへ配る。
    ///          指されていないものには `OnPointerExit` が 1 回だけ届く。
    class IUIInteractable
    {
    public:
        virtual ~IUIInteractable() = default;

        /// @brief 当たりを取る矩形を持つコンポーネント（無ければ当たり判定から外れる）
        virtual RectTransformComponent* GetRectTransform() const = 0;

        /// @brief 今ポインタを受け付けるか（無効にしているボタンなどは false）
        virtual bool AcceptsPointer() const = 0;

        /// @brief 描画順（大きいほど手前。同じなら後から登録された方が手前）
        virtual int GetPointerSortOrder() const = 0;

        /// @brief ポインタが乗った
        virtual void OnPointerEnter() {}

        /// @brief ポインタが外れた
        virtual void OnPointerExit() {}

        /// @brief 自分の上でボタンを押した
        virtual void OnPointerDown() {}

        /// @brief 押していたボタンを離した
        /// @param onSelf 自分の上で離したか（false なら外へ逃げて離した＝クリックにしない）
        virtual void OnPointerUp(bool onSelf) { (void)onSelf; }
    };
}
