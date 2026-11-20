#pragma once

class asIScriptEngine;

namespace CoreEngine
{
    class EngineSystem;
}

namespace CoreEngine::Script
{
    /// @brief 水まわりの関数をスクリプトへ登録する
    /// @details 名前空間 `Water` に、水面を押し下げて波紋を広げる `AddRipple` を出す。
    ///          値型 `Vector3` を先に登録しておくこと。
    /// @param engineSystem 水の波紋を引く先（nullptr なら何もしない）
    /// @return すべて登録できたら true
    bool RegisterWaterBinding(asIScriptEngine* engine, EngineSystem* engineSystem);
}
