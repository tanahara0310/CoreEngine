#pragma once

class asIScriptEngine;

namespace CoreEngine
{
    class EngineSystem;
}

namespace CoreEngine::Script
{
    /// @brief 描画まわりの型と関数をスクリプトへ登録する
    /// @details GameObject から取るマテリアルのハンドル `Material`（色）と、
    ///          描画の設定を読むだけの名前空間 `Rendering`（自動露出の EV）を出す。
    /// @param engineSystem 描画のサービスを引く先（nullptr なら読み取りは既定値を返す）
    /// @return すべて登録できたら true
    bool RegisterRenderingBinding(asIScriptEngine* engine, EngineSystem* engineSystem);
}
