#pragma once
#include "InputConfig.h"
#include "Math/Vector/Vector2.h"
#include <array>
#include <optional>

namespace CoreEngine {

    class KeyboardInput;

    /// @brief 同時に見るゲームパッドの数
    inline constexpr int kMaxGamepads = 4;

    /// @brief どのパッドでもよいことを表すプレイヤー番号
    /// @details 1 人で遊ぶときはこれ。2 人目以降を分けて読むときだけ 0〜3 を渡す。
    inline constexpr int kAnyGamepad = -1;

    /// @brief アクションベースの入力問い合わせクラス
    /// InputConfig のバインディング定義をもとに各デバイスの入力状態を評価する
    class InputQuery {
    public:
        /// @brief デバイスポインタを設定して初期化し、デフォルトバインディングを適用する
        void Initialize(KeyboardInput* keyboard, MouseInput* mouse,
                        const std::array<GamepadInput*, kMaxGamepads>& gamepads);

        /// @brief バインディング設定への参照を取得
        InputConfig& GetConfig() { return config_; }
        const InputConfig& GetConfig() const { return config_; }

        // ─── 場面 ─────────────────────────────────────────────────

        /// @brief いま入力を受け付ける場面を決める
        /// @details メニューを選んでいる間は `UI`、遊んでいる間は `Game`。
        ///          重ならない場面のアクションは、押されていないものとして返る。
        /// @note UI の場面へ移すのは `UIInteractionFeature`（選んでいる相手がいる間）。
        void SetActiveContexts(InputContext contexts) { activeContexts_ = contexts; }

        /// @brief いま入力を受け付けている場面
        InputContext GetActiveContexts() const { return activeContexts_; }

        /// @brief そのアクションがいまの場面で効くか
        bool IsActionActive(InputAction action) const;

        /// @brief キーボードを読まないようにする
        /// @details エディタの入力欄へ文字を打っている間に使う。
        ///          これが無いと、インスペクタで名前を打つ文字がゲームにも届く。
        /// @note キーボードの割り当てだけを外す。マウスとパッドはそのまま読む。
        void SetKeyboardSuppressed(bool suppressed) { keyboardSuppressed_ = suppressed; }
        bool IsKeyboardSuppressed() const { return keyboardSuppressed_; }

        // ─── アクションベース問い合わせ ───────────────────────────

        /// @brief アクションに対応するいずれかの入力が押されているか
        /// @param player 読むパッド。`kAnyGamepad` ならキーボードと全パッドを見る。
        ///        0〜3 を渡すとそのパッドだけを見る（キーボードは見ない）。
        bool IsActionPressed(InputAction action, int player = kAnyGamepad) const;

        /// @brief アクションに対応するいずれかの入力が押された瞬間か
        bool IsActionTriggered(InputAction action, int player = kAnyGamepad) const;

        /// @brief アクションに対応するいずれかの入力が離された瞬間か
        bool IsActionReleased(InputAction action, int player = kAnyGamepad) const;

        /// @brief アクションのアナログ値を取得（0.0〜1.0）
        float GetAxisValue(InputAction action, int player = kAnyGamepad) const;

        /// @brief 2 つのアクションの差（-1.0〜1.0）
        /// @param negative 負の向き（左・下・後ろ）
        /// @param positive 正の向き
        /// @note 両方押していれば 0 になる。
        float GetAxis(InputAction negative, InputAction positive,
                      int player = kAnyGamepad) const;

        /// @brief 4 つのアクションから 2 次元の入力を作る
        /// @note 斜めが長くならないよう、長さが 1 を超えたら丸める
        ///       （キーボードで斜めに動くと速くなるのを防ぐ）。
        Vector2 GetAxis2D(InputAction negativeX, InputAction positiveX,
                          InputAction negativeY, InputAction positiveY,
                          int player = kAnyGamepad) const;

        // ─── キーボード直接アクセス ───────────────────────────────

        /// @brief キーが押され続けているか
        bool IsKeyPressed(uint8_t dikCode) const;
        /// @brief キーが押された瞬間か
        bool IsKeyTriggered(uint8_t dikCode) const;
        /// @brief キーが離された瞬間か
        bool IsKeyReleased(uint8_t dikCode) const;

        /// @brief 読まない設定を無視してキーを見る
        /// @details キーコンフィグ画面が「入力待ちのキャンセル」を拾うためのもの。
        ///          その画面は ImGui の上にあるので、普通に読むと必ず外される。
        bool IsKeyTriggeredRaw(uint8_t dikCode) const;

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
        /// @param player 何番のパッドか（0〜3）
        bool  IsGamepadConnected(int player = 0) const;
        /// @brief 何台つながっているか
        int   GetConnectedGamepadCount() const;
        /// @brief 左スティックの傾き
        Stick GetLeftStick(int player = 0) const;
        /// @brief 右スティックの傾き
        Stick GetRightStick(int player = 0) const;
        /// @brief 左トリガーの踏み込み量（0.0〜1.0）
        float GetLeftTrigger(int player = 0) const;
        /// @brief 右トリガーの踏み込み量（0.0〜1.0）
        float GetRightTrigger(int player = 0) const;

        /// @brief 振動させる（0.0〜1.0）
        /// @param leftMotorRatio 低い唸り
        /// @param rightMotorRatio 高い震え
        /// @param player 何番のパッドか（0〜3）
        void SetVibration(float leftMotorRatio, float rightMotorRatio, int player = 0);

        // ─── キーコンフィグ用 ─────────────────────────────────────

        /// @brief 今フレームで押された物理入力をバインディングとして返す
        /// キー / ボタンは「押した瞬間」、アナログ軸（スティック・トリガー）は
        /// 十分に倒し込まれている状態をしきい値で判定する
        /// @return 検出されたバインディング（何も押されていない場合は nullopt）
        std::optional<InputBinding> DetectAnyInput() const;

    private:
        /// @brief 一緒に押すキーが揃っているか
        /// @details 指定したものが押されていることだけを見る（指定していないキーは問わない）。
        bool  ModifiersHeld(InputModifier modifiers) const;

        /// @brief いま押している修飾キー
        InputModifier CurrentModifiers() const;

        /// @brief プレイヤー番号のパッド（範囲外・未接続なら nullptr）
        GamepadInput* GamepadAt(int player) const;

        /// @brief 対象のパッドを順に見て、どれかが満たせば true
        /// @param player `kAnyGamepad` なら全部、0〜3 ならその 1 台だけ
        template <class Fn>
        bool AnyGamepad(int player, Fn&& check) const
        {
            if (player != kAnyGamepad) {
                GamepadInput* const pad = GamepadAt(player);
                return pad && check(*pad);
            }
            for (GamepadInput* const pad : gamepads_) {
                if (pad && pad->IsConnected() && check(*pad)) {
                    return true;
                }
            }
            return false;
        }

        /// @brief 対象のパッドのうち、いちばん大きい値
        template <class Fn>
        float MaxGamepad(int player, Fn&& value) const
        {
            if (player != kAnyGamepad) {
                GamepadInput* const pad = GamepadAt(player);
                return pad ? value(*pad) : 0.0f;
            }
            float best = 0.0f;
            for (GamepadInput* const pad : gamepads_) {
                if (!pad || !pad->IsConnected()) {
                    continue;
                }
                const float current = value(*pad);
                if (current > best) {
                    best = current;
                }
            }
            return best;
        }

        bool  EvaluatePressed  (const InputBinding& b, int player) const;
        bool  EvaluateTriggered(const InputBinding& b, int player) const;
        bool  EvaluateReleased (const InputBinding& b, int player) const;
        float EvaluateAxis     (const InputBinding& b, int player) const;

        InputConfig    config_;

        // いま入力を受け付ける場面。エディタの操作はエディタのビルドでだけ効く
        InputContext   activeContexts_ = InputContext::Game | kEditorContext;

        // エディタの入力欄へ文字を打っている間だけ立つ
        bool           keyboardSuppressed_ = false;

        KeyboardInput* keyboard_ = nullptr;
        MouseInput*    mouse_    = nullptr;
        std::array<GamepadInput*, kMaxGamepads> gamepads_{};
    };

}
