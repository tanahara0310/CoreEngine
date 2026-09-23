#pragma once

#include "Math/Vector/Vector2.h"

namespace CoreEngine
{
    class WinApp;
    class InputManager;
    class GameOutputWindow;

    /// @brief ゲーム画面の上のポインタ（UI の当たり判定が使う）
    /// @details UI はキャンバス座標（基準解像度・左上が原点）で当たりを取るが、
    ///          マウスは画面のピクセルで来る。その間を埋めるのがここ。
    /// @note ゲーム画面が画面上のどこにあるかは 3 通りある。
    ///       エディタの Game ビュー（毎フレーム `SetViewRect` で教わる）、
    ///       ゲーム映像専用ウィンドウ（開いていればそちら）、
    ///       本体ウィンドウのクライアント領域（どちらも無いとき）。
    class UIPointer
    {
    public:
        static UIPointer& Get();

        /// @brief ゲーム画面が画面上のどこにあるかを教える（画面座標のピクセル）
        /// @param origin 画像の左上
        /// @param size 画像の大きさ
        /// @param acceptsPointer その画像がポインタを受け付ける状態か
        ///        （別の窓が上にある・掴んでいる最中などは false）
        /// @note エディタが Game ビューを描くたびに呼ぶ。呼ばれなかったフレームは
        ///       ゲーム映像専用ウィンドウか本体ウィンドウへ落ちる。
        void SetViewRect(const Vector2& origin, const Vector2& size, bool acceptsPointer);

        /// @brief このフレームの位置とボタンを決める
        /// @note UI の走査より前に 1 回だけ呼ぶ。
        void Update(const WinApp* winApp, const InputManager* input,
                    const GameOutputWindow* outputWindow);

        /// @brief ポインタがゲーム画面の上にあるか
        bool IsOver() const { return isOver_; }

        /// @brief ゲーム画面の中での位置（0〜1）。外にいるときの値は使わないこと
        const Vector2& GetNormalized() const { return normalized_; }

        /// @brief キャンバス座標での位置
        /// @param canvasSize UI の基準解像度（`UIRenderer::GetScreenSize()`）
        Vector2 ToCanvas(const Vector2& canvasSize) const
        {
            return { normalized_.x * canvasSize.x, normalized_.y * canvasSize.y };
        }

        /// @brief 左ボタンをこのフレームに押したか
        bool IsPressed() const { return pressed_; }

        /// @brief 左ボタンを押し続けているか
        bool IsHeld() const { return held_; }

        /// @brief 左ボタンをこのフレームに離したか
        bool IsReleased() const { return released_; }

    private:
        UIPointer() = default;
        ~UIPointer() = default;
        UIPointer(const UIPointer&) = delete;
        UIPointer& operator=(const UIPointer&) = delete;

        /// @brief このフレームに教わったゲーム画面の矩形を取り出す（無ければ false）
        bool ConsumeViewRect(Vector2& outOrigin, Vector2& outSize, bool& outAccepts);

        // 教わった矩形（1 フレーム分。使ったら消える）
        Vector2 viewOrigin_{};
        Vector2 viewSize_{};
        bool viewAccepts_ = false;
        bool hasViewRect_ = false;

        Vector2 normalized_{};
        bool isOver_ = false;
        bool pressed_ = false;
        bool held_ = false;
        bool released_ = false;
    };
}
