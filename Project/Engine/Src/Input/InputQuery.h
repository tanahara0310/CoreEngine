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

        bool IsKeyPressed(uint8_t dikCode) const;
        bool IsKeyTriggered(uint8_t dikCode) const;
        bool IsKeyReleased(uint8_t dikCode) const;

        // ─── マウス直接アクセス ───────────────────────────────────

        bool IsMouseButtonPressed(MouseButton button) const;
        bool IsMouseButtonTriggered(MouseButton button) const;
        bool IsMouseButtonReleased(MouseButton button) const;
        int  GetMouseDragX() const;
        int  GetMouseDragY() const;
        int  GetWheelDelta() const;
        POINT GetCursorPosition() const;

        // ─── ゲームパッド直接アクセス ─────────────────────────────

        bool  IsGamepadConnected() const;
        Stick GetLeftStick() const;
        Stick GetRightStick() const;
        float GetLeftTrigger() const;
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
