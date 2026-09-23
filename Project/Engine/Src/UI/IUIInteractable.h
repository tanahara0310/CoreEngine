#pragma once

#include "Math/Vector/Vector2.h"
#include "UI/UINavigation.h"

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

        /// @brief ポインタで押したまま動かしている（押し始めたフレームを含め、離すまで毎フレーム）
        /// @param pointerOnCanvas キャンバス座標でのポインタの位置
        /// @param canvasSize UI の基準解像度
        /// @note 決定キーで押しているときは届かない（キーには位置が無いため）。
        virtual void OnDrag(const Vector2& pointerOnCanvas, const Vector2& canvasSize)
        {
            (void)pointerOnCanvas;
            (void)canvasSize;
        }

        /// @brief フォーカス中に向きの入力が来た
        /// @param direction 入力された向き
        /// @return true を返すと、その向きへのフォーカス送りは起きない
        /// @note つまみを動かすスライダーのように、向きを自分で使う UI が true を返す。
        virtual bool OnNavigate(UINavigationDirection direction)
        {
            (void)direction;
            return false;
        }
    };
}
