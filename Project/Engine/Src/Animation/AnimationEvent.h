#pragma once

#include <string>

namespace CoreEngine
{
    /// @brief クリップの決まった時刻に出た印（同じオブジェクトのコンポーネント全部に届く）
    struct AnimationEvent
    {
        std::string name;
        std::string clip;   ///< クリップの id
        std::string state;  ///< 状態の名前
        std::string layer;  ///< レイヤーの名前
        float floatValue = 0.0f;
        int intValue = 0;
        std::string stringValue;
    };
}
