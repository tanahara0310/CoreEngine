#pragma once

class asIScriptEngine;

namespace CoreEngine::Script
{
    /// @brief CVar の読み書きをスクリプトへ登録する
    /// @details 名前空間 `CVar` として、名前で引いて型ごとに読み書きする関数を出す。
    ///          368 個ある値を 1 つずつ束縛する代わりに、レジストリを名前で引く
    ///          （UI とシリアライズが既にそうしているのと同じ作り）。
    ///          ポストエフェクトは `r.<効果>.Enabled` を書けば次のフレームから効く
    ///          （`PostEffectManager::PrepareFrame` が CVar の通番を見て組み直す）。
    /// @return すべて登録できたら true
    bool RegisterCVarBinding(asIScriptEngine* engine);
}
