#pragma once

#include <array>
#include <cstdint>

#include "Math/Vector/Vector3.h"

namespace CoreEngine
{
class Collider;
class GameObject;

/// @brief 水を押しのける形を分ける塊の数（縦横高さ 2 つずつ）
inline constexpr uint32_t kDisplacementCellCount = 8;

/// @brief 形を分けた塊 1 つ（オーナーの軸に沿った、スケール込みの大きさ）
/// @details 塊の番号の 1 ビット目は X、2 ビット目は Y、3 ビット目は Z の正の側を表す
struct DisplacementCell {
    Vector3 localCenter{}; ///< オーナーの位置から塊の中心まで（オーナーの軸の成分）
    Vector3 size{};        ///< 塊の 3 辺の長さ
};

/// @brief 塊 1 つのワールドでの置き場所
struct PlacedDisplacementCell {
    Vector3 lever{};      ///< オーナーの位置から塊の中心まで（ワールドの向き）
    Vector3 center{};     ///< 塊の中心（ワールド）
    float height = 0.0f;  ///< 回った塊を囲む箱の上下の幅 [m]
    float area = 0.0f;    ///< 回った塊を真上から見た広さ [m²]
};

using DisplacementCells = std::array<DisplacementCell, kDisplacementCellCount>;
using PlacedDisplacementCells = std::array<PlacedDisplacementCell, kDisplacementCellCount>;

/// @brief どの形にも当てはまらないときの形
enum class DisplacementFallbackShape {
    Sphere,  ///< 半径 0.5 の球をオーナーのいちばん大きいスケールで伸ばす
    UnitBox, ///< 1 辺 1 の箱をオーナーのスケールで伸ばす
};

/// @brief コライダーの形とオーナーのスケールから、形を囲む箱を 8 つの塊に分ける
/// @param collider 形（nullptr なら fallback の形）
/// @return 形全体の体積 [m³]
float BuildDisplacementCells(
    const GameObject& owner, const Collider* collider, DisplacementFallbackShape fallback, DisplacementCells& cells);

/// @brief 塊をオーナーの今の位置と向きへ置く
void PlaceDisplacementCells(const GameObject& owner, const DisplacementCells& cells, PlacedDisplacementCells& out);
}
