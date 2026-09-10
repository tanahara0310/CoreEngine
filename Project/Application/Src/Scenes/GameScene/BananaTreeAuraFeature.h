#pragma once

#include "Scene/Feature/ISceneFeature.h"

#include <memory>

namespace GameComponents
{
    /// @brief バナナの木の隣接マスへ、収穫範囲を示す四角い波動を描く Feature を作る
    /// @details 確定済み・未確定を問わず、レールが置かれたマスの波動は表示しない。
    ///          見た目は CVar `Game.BananaTreeAura.*` から調整できる。
    std::unique_ptr<CoreEngine::ISceneFeature> CreateBananaTreeAuraFeature();
}
