#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Core/ObjectRef.h"
#include "Math/Vector/Vector2.h"
#include "Reflection/Reflect.h"
#include "UI/UIElement.h"

#include <cstdint>

namespace CoreEngine
{
    class RectTransformComponent;
    class UISliderComponent;

    /// @brief 自分の四角を窓にして、子の「中身」をホイール・ドラッグ・フォーカスで動かすコンポーネント
    /// @details 中身は自分の子にする。窓の外を描かず押せなくするには、同じ物に UIMask を付ける。
    ///          中身は窓の端を越えて動かない（跳ね返らない）。中身が窓より小さい向きは先頭に止める。
    ///          ポインタで押して `kDragThreshold` 動かすとドラッグになり、押していたボタンは押されなかったことになる。
    ///          キー・パッドで中の物を選ぶと、それが窓に入るまで中身を動かす。
    /// @note 動かすのは再生中だけ。時間はポーズで止まらない方で測る。
    class UIScrollViewComponent final : public IComponent
    {
    public:
        /// @brief 押してからドラッグとみなすまでの動き（キャンバスの px）
        static constexpr float kDragThreshold = 8.0f;

        /// @brief ホイール 1 目盛りの量を中身から決められないときの量（px）
        static constexpr float kFallbackWheelStep = 40.0f;

        const char* GetTypeName() const override { return "UIScrollView"; }

        REFLECT_DECLARE(UIScrollViewComponent)

        /// @brief UI トランスフォームを使う
        bool RequiresComponent(const IComponent& other) const override;

        /// @brief UI トランスフォームを確保する
        void Awake() override;

        /// @brief 慣性で動かし、端で止め、スクロールバーと合わせる
        void LateUpdate() override;

        // ===== 位置 =====

        /// @brief 中身の位置（0 = 先頭（左・上）/ 1 = 末尾（右・下）。動けない向きは 0）
        Vector2 GetNormalizedPosition() const;
        void SetNormalizedPosition(const Vector2& normalized);

        /// @brief 中身の動く速さ（px／秒。右・下が正）
        Vector2 GetVelocity() const { return velocity_; }
        void SetVelocity(const Vector2& velocity) { velocity_ = velocity; }

        /// @brief 慣性を止める
        void StopMovement();

        /// @brief target（中身の子孫）が窓に入るまで中身を動かす
        /// @note 窓より大きい物は先頭（左・上）を窓の先頭に合わせる。
        void ScrollTo(const RectTransformComponent& target);

        // ===== 入力（UIInteractionFeature が呼ぶ） =====

        /// @brief ホイールで動かす（正で中身が下・右へ動き、前の行が見える）
        /// @param notches 回した目盛りの数
        void Scroll(float notches);

        /// @brief ドラッグを始める（押した所から中身がポインタに付いてくる）
        void BeginDrag(const Vector2& pointerOnCanvas, const Vector2& canvasSize);

        /// @brief ドラッグ中のポインタの位置を受け取る（毎フレーム）
        void Drag(const Vector2& pointerOnCanvas, const Vector2& canvasSize);

        /// @brief ドラッグをやめる（速さが残っていれば慣性で動き続ける）
        void EndDrag();

        bool IsDragging() const { return dragging_; }

        // ===== 設定 =====

        bool IsHorizontal() const { return horizontal_; }
        void SetHorizontal(bool enabled) { horizontal_ = enabled; }
        bool IsVertical() const { return vertical_; }
        void SetVertical(bool enabled) { vertical_ = enabled; }

        /// @brief 自分の UI トランスフォーム（窓）
        RectTransformComponent* GetRectTransform() const;

        /// @brief 中身（繋いでいなければ nullptr）
        RectTransformComponent* GetContent() const;
        void SetContent(RectTransformComponent* content);

        /// @brief 縦・横のスクロールバー（繋がなくてもよい）
        void SetVerticalScrollbar(UISliderComponent* scrollbar);
        void SetHorizontalScrollbar(UISliderComponent* scrollbar);

    private:
        /// @brief 窓の四角と中身の四角（どちらも窓の座標・px）
        /// @return 中身を繋いでいない・中身が自分の子でないときは false
        bool MeasureView(UIRect& outView, UIRect& outContent) const;

        /// @brief 窓の先頭から中身の先頭までのずれ（右・下が正。動ける範囲は 0 以下）
        static Vector2 OffsetOf(const UIRect& view, const UIRect& content);

        /// @brief 中身をずらし、端を越えた分は止める。止めた向きの速さは 0 にする
        /// @param respectAxes true なら動かせる向き（横・縦）だけを動かす
        void MoveContent(const Vector2& delta, bool respectAxes);

        /// @brief ホイール 1 目盛りの量（向き 0 = 横・1 = 縦）
        float WheelStep(int axis) const;

        /// @brief スクロールバーを動かされたら中身を合わせ、動かされていなければスクロールバーを中身に合わせる
        void SyncScrollbars();

        ObjectRef<RectTransformComponent> content_;
        bool horizontal_ = false;
        bool vertical_ = true;
        bool inertia_ = true;
        float decelerationRate_ = 0.135f;
        float wheelStep_ = 0.0f;
        ObjectRef<UISliderComponent> horizontalScrollbar_;
        ObjectRef<UISliderComponent> verticalScrollbar_;

        mutable RectTransformComponent* rect_ = nullptr;

        Vector2 velocity_{};
        bool dragging_ = false;
        /// ドラッグを始めたときのポインタ（窓の座標）と中身のずれ
        Vector2 dragStartPointer_{};
        Vector2 dragStartOffset_{};
        /// 前のフレームのドラッグで置いた中身のずれ（速さを測る）
        Vector2 lastDragOffset_{};
        /// 最後にドラッグを受け取ったフレーム
        uint64_t lastDragFrame_ = 0;
        /// 最後にスクロールバーへ書いた値（負は書いていない）
        float scrollbarWritten_[2] = { -1.0f, -1.0f };
    };
}
