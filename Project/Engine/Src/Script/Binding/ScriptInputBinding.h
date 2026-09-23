#pragma once

class asIScriptEngine;

namespace CoreEngine
{
    class InputManager;
}

namespace CoreEngine::Script
{
    /// @brief 入力の問い合わせをスクリプトへ登録する
    /// @details 列挙 `InputAction` と、名前空間 `Input` の `IsActionPressed` / `IsActionTriggered` /
    ///          `IsActionReleased` / `GetAxisValue` / `IsGamepadConnected` を登録する。
    /// @param input 問い合わせ先（nullptr なら、どの関数も押されていない・つながっていないを返す）
    /// @return すべて登録できたら true
    bool RegisterInputBinding(asIScriptEngine* engine, InputManager* input);
}
