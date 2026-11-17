#pragma once

#include "Particle/Core/ParticleRenderDataBuilder.h" // BillboardType

#include <cstddef>
#include <iterator>

namespace CoreEngine
{
    /// @brief パーティクルのビルボードの名前（`BillboardType` の並び。CPU 版と GPU 版のインスペクタが使う）
    inline constexpr const char* kParticleBillboardNames[] = { "なし（粒の回転だけ）", "カメラの正面", "Y 軸だけ回す", "画面と平行" };
    static_assert(std::size(kParticleBillboardNames) == static_cast<std::size_t>(BillboardType::ScreenAligned) + 1);
}
