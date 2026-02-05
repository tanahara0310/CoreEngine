#pragma once
#include "NodeAnimation.h"
#include <map>
#include <string>

/// @brief アニメーション全体を表現する構造体
/// 複数のNodeAnimationで構成される

namespace CoreEngine
{
struct Animation {
    float duration;  //!< アニメーション全体の尺(単位は秒)
    
    // NodeAnimationの集合。Node名でひけるようにしておく
    std::map<std::string, NodeAnimation> nodeAnimations;
};
}
