#pragma once

namespace CoreEngine::Reflection
{
    /// @brief インスペクタと保存を型記述子から組み立てるか
    /// @return false なら従来の DrawInspector / OnSerialize を使う
    bool IsEnabled();
}
