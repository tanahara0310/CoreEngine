#pragma once

class asIScriptEngine;

namespace CoreEngine
{
    class EngineSystem;
}

namespace CoreEngine::Script
{
    /// @brief UI の型をスクリプトへ登録する
    /// @details 列挙 `UIAnchor` / `TextAlignH` / `TextAlignV`、GameObject の `uiText` / `uiImage` から取れる
    ///          `UIText` / `UIImage` のハンドル、同じシーンへ UI 要素を作る `GameObject.SpawnUIText` / `SpawnUIImage`、
    ///          名前付きのフォントを登録する `Font::Register` を出す。
    ///          型 `GameObject`・値型 `Vector2` / `Vector4`・`array<T>`・`string` を先に登録しておくこと。
    /// @param engineSystem フォントの管理を引く先（nullptr なら `Font::Register` は警告だけ出す）
    /// @return すべて登録できたら true
    bool RegisterUIBinding(asIScriptEngine* engine, EngineSystem* engineSystem);
}
