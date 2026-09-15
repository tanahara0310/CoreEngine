#pragma once

class asIScriptEngine;

namespace CoreEngine::Script
{
    /// @brief シーンをまたぐ値の置き場と、保存データのファイルをスクリプトへ登録する
    /// @details 名前空間 `Session`（SessionValues の読み書き）と、`Application/Saved` の下の JSON ファイルを
    ///          読み書きする参照型 `SaveFile` を出す。
    /// @return すべて登録できたら true
    bool RegisterSessionBinding(asIScriptEngine* engine);
}
