#pragma once

class asIScriptEngine;

namespace CoreEngine::Script
{
    /// @brief Tween の型と関数をスクリプトへ登録する
    /// @details 列挙 `TweenLoop` / `TweenUpdate`、呼び戻しの funcdef、値型 `TweenHandle` / `TweenSequence`、
    ///          名前空間 `Tween` の生成口を出す。値の書き込みはスクリプトの関数（セッター）を通す。
    ///          列挙 `EaseType` と型 `GameObject` を先に登録しておくこと。
    /// @return すべて登録できたら true
    bool RegisterTweenBinding(asIScriptEngine* engine);
}
