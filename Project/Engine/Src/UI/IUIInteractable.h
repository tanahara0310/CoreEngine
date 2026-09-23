#pragma once

namespace CoreEngine
{
    class RectTransformComponent;

    /// @brief ポインタとフォーカスで操作できる UI が実装する口
    /// @details `UIInteractionFeature` が毎フレーム、ポインタはいちばん手前の 1 つだけへ、
    ///          フォーカスは選んでいる 1 つだけへ配る。
    ///          指されていないものには `OnPointerExit` が 1 回だけ届く。
    /// @note 押した・離したはポインタでも決定キーでも同じ口へ届く。
    class IUIInteractable
    {
    public:
        virtual ~IUIInteractable() = default;

        /// @brief 当たりを取る矩形を持つコンポーネント（無ければ当たり判定から外れる）
        virtual RectTransformComponent* GetRectTransform() const = 0;

        /// @brief 今ポインタとフォーカスを受け付けるか（無効にしているボタンなどは false）
        virtual bool AcceptsInput() const = 0;

        /// @brief 描画順（大きいほど手前。同じなら後から登録された方が手前）
        /// @note ポインタが重なったときの前後にだけ使う。フォーカス送りは位置で決める。
        virtual int GetPointerSortOrder() const = 0;

        /// @brief ポインタが乗った
        virtual void OnPointerEnter() {}

        /// @brief ポインタが外れた
        virtual void OnPointerExit() {}

        /// @brief フォーカスが移ってきた（キー・パッドでの送り先になった）
        virtual void OnFocusEnter() {}

        /// @brief フォーカスが外れた
        virtual void OnFocusExit() {}

        /// @brief 押された（自分の上でポインタを押した、またはフォーカス中に決定キーを押した）
        virtual void OnPressBegin() {}

        /// @brief 押していたものを離した
        /// @param onSelf 自分の上で離したか
        ///        （false なら外へ逃げて離した・押している間にフォーカスが移った＝クリックにしない）
        virtual void OnPressEnd(bool onSelf) { (void)onSelf; }
    };
}
