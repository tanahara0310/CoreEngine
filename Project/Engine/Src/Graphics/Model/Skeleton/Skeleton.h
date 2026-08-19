#pragma once
#include "Joint.h"
#include <map>
#include <string>

namespace CoreEngine
{
/// @brief Skeleton構造体
struct Skeleton {
    int32_t root;      // RootJointのIndex
    std::map<std::string, int32_t> jointMap;   // Joint名とIndexの辞書
    std::vector<Joint> joints;          // 所属しているジョイント
};
}
