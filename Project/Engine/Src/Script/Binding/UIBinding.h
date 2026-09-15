#pragma once

class asIScriptEngine;

namespace CoreEngine::Script
{
    /// @brief UI の型をスクリプトへ登録する
    /// @details 列挙 `UIAnchor` と、GameObject の `uiText` から取れる `UIText` のハンドルを出す。
    ///          型 `GameObject` と値型 `Vector2` / `Vector4` を先に登録しておくこと。
    /// @return すべて登録できたら true
    bool RegisterUIBinding(asIScriptEngine* engine);
}
