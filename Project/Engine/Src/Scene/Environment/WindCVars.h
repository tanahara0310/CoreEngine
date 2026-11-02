#pragma once

#include "Math/Vector/Vector2.h"
#include "Utility/CVar/CVar.h"

namespace CoreEngine
{
    /// @brief 地上の風（シーンの `_environment.json` に保存する）
    /// @details 水面の風波・泡・植物の揺れが読む。雲は上空の風 `r.Cloud.*` を別に持つ。
    namespace WindCVars
    {
        extern CVar<float>   Speed;
        extern CVar<Vector2> Direction;

        /// @brief 風が吹いていく向き（XZ。長さ 1。長さ 0 なら +X）
        Vector2 NormalizedDirection();
    }
}
