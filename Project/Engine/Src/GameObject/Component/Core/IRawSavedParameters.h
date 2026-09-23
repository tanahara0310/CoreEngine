#pragma once

#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    /// @brief 保存データをそのまま持ち続けるコンポーネントの口
    /// @details 型が見つからないコンポーネント（`MissingComponent`）と、クラスが見つからない間の
    ///          スクリプトのコンポーネントだけが実装する。読んだ値をそのまま書き戻すためのもので、
    ///          普通のコンポーネントは値を型記述子（`REFLECT_PROPERTY`）で宣言する。
    class IRawSavedParameters
    {
    public:
        virtual ~IRawSavedParameters() = default;

        /// @brief 書き戻す保存データ（オブジェクトでなければ何も書き戻さない）
        virtual const json& GetRawParameters() const = 0;

        /// @brief 読んだ保存データを控える
        virtual void SetRawParameters(const json& parameters) = 0;
    };
}
