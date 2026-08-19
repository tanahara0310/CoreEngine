#pragma once
#include "InputConfig.h"
#include <optional>

namespace CoreEngine {

    class KeyboardInput;

    /// @brief アクションベースの入力問い合わせクラス
    /// InputConfig のバインディング定義をもとに各デバイスの入力状態を評価する
    class InputQuery {
    public:
        /// @brief デバイスポインタを設定して初期化し、デフォルトバインディングを適用する
        void Initialize(KeyboardInput* keyboard, MouseInput* mouse, GamepadInput* gamepad);

        /// @brief バインディング設定への参照を取得
        InputConfig& GetConfig() { return config_; }
        const InputConfig& GetConfig() const { return config_; }

        // ─── アクションベース問い合わせ ───────────────────────────

        /// @brief アクションに対応するいずれかの入力が押されているか
        bool IsActionPressed(InputAction action) const;

        /// @brief アクションに対応するいずれかの入力が押された瞬間か
        bool IsActionTriggered(InputAction action) const;

        /// @brief アクションに対応するいずれかの入力が離された瞬間か
        bool IsActionReleased(InputAction action) const;

        /// @brief アクションのアナログ値を取得（0.0〜1.0）
        float GetAxisValue(InputAction action) const;

        // ─── キーボード直接アクセス ───────────────────────────────

        /// @brief キーが押され続けているか
        bool IsKeyPressed(uint8_t dikCode) const;
        /// @brief キーが押された瞬間か
        bool IsKeyTriggered(uint8_t dikCode) const;
        /// @brief キーが離された瞬間か
        bool IsKeyReleased(uint8_t dikCode) const;

        // ─── マウス直接アクセス ───────────────────────────────────

        /// @brief マウスボタンが押され続けているか
        bool IsMouseButtonPressed(MouseButton button) const;
        /// @brief マウスボタンが押された瞬間か
        bool IsMouseButtonTriggered(MouseButton button) const;
        /// @brief マウスボタンが離された瞬間か
        bool IsMouseButtonReleased(MouseButton button) const;
        /// @brief 前フレームからのマウス X 移動量 [px]
        int  GetMouseDragX() const;
        /// @brief 前フレームからのマウス Y 移動量 [px]
        int  GetMouseDragY() const;
        /// @brief ホイールの移動量（1 ノッチ = 120）
        int  GetWheelDelta() const;
        /// @brief カーソルのスクリーン座標
        POINT GetCursorPosition() const;

        // ─── ゲームパッド直接アクセス ─────────────────────────────

        /// @brief ゲームパッドが接続されているか
        bool  IsGamepadConnected() const;
        /// @brief 左スティックの傾き
        Stick GetLeftStick() const;
        /// @brief 右スティックの傾き
        Stick GetRightStick() const;
        /// @brief 左トリガーの踏み込み量（0.0〜1.0）
        float GetLeftTrigger() const;
        /// @brief 右トリガーの踏み込み量（0.0〜1.0）
        float GetRightTrigger() const;

        // ─── キーコンフィグ用 ─────────────────────────────────────

        /// @brief 今フレームで押された物理入力をバインディングとして返す
        /// @return 検出されたバインディング（何も押されていない場合は nullopt）
        std::optional<InputBinding> DetectAnyInput() const;

    private:
        bool  EvaluatePressed  (const InputBinding& b) const;
        bool  EvaluateTriggered(const InputBinding& b) const;
        bool  EvaluateReleased (const InputBinding& b) const;
        float EvaluateAxis     (const InputBinding& b) const;

        InputConfig    config_;
        KeyboardInput* keyboard_ = nullptr;
        MouseInput*    mouse_    = nullptr;
        GamepadInput*  gamepad_  = nullptr;
    };

}
