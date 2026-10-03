#pragma once

namespace CoreEngine
{
    /// @brief スクリプトの組み立ての状態
    enum class ScriptBuildState
    {
        NotBuilt,     ///< 一度も組めていない（スクリプトのコンポーネントは使えない）
        Ok,           ///< 全部のファイルが通った
        RunningStale, ///< エラーのあるファイルだけ直す前の版で組み、動いている
        KeptPrevious, ///< 新しい版を組めず、前のモジュールのまま動いている
    };
}
