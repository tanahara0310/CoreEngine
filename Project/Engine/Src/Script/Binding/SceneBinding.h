#pragma once

class asIScriptEngine;

namespace CoreEngine
{
    class EngineSystem;
}

namespace CoreEngine::Script
{
    /// @brief シーンの切り替えをスクリプトへ登録する（名前空間 `Scene`）
    /// @param engine 登録先のスクリプトのエンジン
    /// @param engineSystem シーンマネージャを引く先（nullptr なら何もしない）
    /// @return すべて登録できたら true
    bool RegisterSceneBinding(asIScriptEngine* engine, EngineSystem* engineSystem);
}
