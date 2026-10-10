#pragma once

#include "Math/Vector/Vector2.h"

#include <cstddef>

namespace CoreEngine::KeyCurve
{
    /// @brief 点（x, y）を通るなめらかな曲線の、x での値
    /// @details 点は x の小さい順に並べ直して使う。隣り合う点の間は、点の値を行き過ぎない 3 次の曲線（Fritsch–Carlson）でつなぐ。
    ///          最初の点より前と最後の点より後は、端の点の値。点が無ければ 0。
    float Evaluate(const Vector2* keys, std::size_t count, float x);
}
