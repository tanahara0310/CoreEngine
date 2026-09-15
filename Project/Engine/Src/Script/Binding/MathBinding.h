#pragma once

class asIScriptEngine;

namespace CoreEngine::Script
{
    /// @brief ベクトルと数値の関数をスクリプトへ登録する
    /// @details `Vector2` / `Vector3` / `Vector4` を値型として登録し、演算子と
    ///          `Dot` / `Cross` / `Length` / `LengthSquared` / `Distance` / `Normalize` / `Lerp` / `Clamp` /
    ///          `Saturate` / `Min` / `Max` を登録する。ベクトルを 0 で割るとスクリプトの例外にする。
    /// @return すべて登録できたら true
    bool RegisterMathBinding(asIScriptEngine* engine);
}
