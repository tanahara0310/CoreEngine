#pragma once

class asIScriptEngine;

namespace CoreEngine::Script
{
    /// @brief 乱数をスクリプトへ登録する
    /// @details 種を決めて同じ列を出す参照型 `RandomStream`（std::mt19937 と標準の一様分布）と、
    ///          エンジン全体の乱数（RandomGenerator）を使う名前空間 `Random` を出す。
    /// @return すべて登録できたら true
    bool RegisterRandomBinding(asIScriptEngine* engine);
}
