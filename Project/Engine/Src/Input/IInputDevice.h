#pragma once

namespace CoreEngine
{
/// @brief 入力デバイス基底インターフェース
/// Update() のみを共通契約とする。初期化方法はデバイス種別によって異なるため派生インターフェースで定義する
class IInputDevice {
public:
    virtual ~IInputDevice() = default;
        /// @brief 1 フレーム分の入力状態を取り込む（InputManager が毎フレーム呼ぶ）
    virtual void Update() = 0;

    /// @brief 今フレームの入力をすべて「離している」状態にする
    /// @details 別のアプリを触っている間に、取り込む代わりに呼ぶ。
    ///          前フレームの状態は残すので、押していたキーの「離した」は 1 回だけ流れる。
    /// @note 取り込みを止めるだけだと、切り替えた瞬間に押していたキーが
    ///       押しっぱなしとして残り、戻ってきたときに勝手に動く。
    virtual void Reset() = 0;
};
}
