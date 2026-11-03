#pragma once

#include <array>
#include <cstdint>

#include "GameObject/Component/Core/IComponent.h"
#include "Graphics/Water/Query/WaterQuery.h"
#include "Math/Vector/Vector3.h"
#include "Physics/PhysicsKey.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
class RigidbodyComponent;

/// @brief 水に浮かせる（兄弟の剛体へ浮力と水の抵抗を加える）
/// @details コライダーの形を縦横高さ 2 つずつの 8 つの塊に分け、塊ごとに水面の高さを調べて
///          沈んだ体積の分だけ上向きの力をその位置へ加える。力は塊ごとの位置へ加わり、傾いた物を起こす回転も生む。
///          水の抵抗は塊ごとに、速さに比例する分と、動く向きから見た面積で速さの 2 乗に比例する分を加える。
///          水面の高さは描かれる水面と同じで、1〜2 フレーム遅れて届く（WaterQuery）。
class BuoyancyComponent : public IComponent {
public:
    /// @brief 形を分ける塊の数
    static constexpr uint32_t kSampleCount = 8;

    /// @brief 水の中を速く動くときの抵抗の係数（形の抵抗係数）
    static constexpr float kDragCoefficient = 1.0f;

    const char* GetTypeName() const override { return "Buoyancy"; }

    REFLECT_BEGIN(BuoyancyComponent, "浮力")
        REFLECT_PROPERTY(waterDensity_, "水の密度", p.range = Range(1.0f, 20000.0f, 1.0f),
            p.tooltip = "kg/m³。海水は約 1025、真水は 1000。剛体の質量 ÷ 体積がこれより小さいと浮く")
        REFLECT_PROPERTY(volume_, "体積", p.range = Range(0.0f, 100000.0f, 0.01f),
            p.tooltip = "m³。0 ならコライダーの形と大きさから求める")
        REFLECT_PROPERTY(waterDrag_, "水の抵抗", p.range = Range(0.0f, 20.0f, 0.01f),
            p.tooltip = "1/s。沈んでいる分だけ、水の中を動く速さを落とす。速く動くときの抵抗は形の大きさから求めて足す")
        REFLECT_PROPERTY(waterAngularDrag_, "水の回転の抵抗", p.range = Range(0.0f, 20.0f, 0.01f),
            p.tooltip = "1/s。沈んでいる分だけ回転を落とす")
        REFLECT_READONLY_ACCESSOR("submergedRatio", "沈んでいる割合", GetSubmergedRatio)
        REFLECT_READONLY_ACCESSOR("inWater", "水に触れている", IsInWater)
    REFLECT_END()

    /// @brief 剛体を使う
    bool RequiresComponent(const IComponent& other) const override;

    /// @brief 水面の高さを調べる点を足す
    void OnEnable() override;

    /// @brief 水面の高さを調べる点を外す
    void OnDisable() override;

    /// @brief 物理の 1 ステップの前に、浮力と水の抵抗を剛体へ加える（PhysicsFeature が呼ぶ）
    /// @param deltaTime このステップの秒数
    /// @param gravity   物理の重力
    void ApplyForces(PhysicsKey, float deltaTime, const Vector3& gravity);

    /// @brief 沈んでいる体積の割合（0 = 水の外・1 = 全部が水の中）
    float GetSubmergedRatio() const { return submergedRatio_; }

    /// @brief 少しでも水に触れているか
    bool IsInWater() const { return submergedRatio_ > 0.0f; }

    float GetWaterDensity() const { return waterDensity_; }
    void SetWaterDensity(float density) { waterDensity_ = density; }

    float GetVolume() const { return volume_; }
    void SetVolume(float volume) { volume_ = volume; }

private:
    /// @brief 形を分けた塊 1 つ（オーナーの軸に沿った、スケール込みの大きさ）
    struct Cell {
        Vector3 localCenter{}; ///< オーナーの位置から塊の中心まで（オーナーの軸の成分）
        Vector3 size{};        ///< 塊の 3 辺の長さ
    };

    /// @brief 兄弟のコライダーの形とオーナーのスケールから塊を作り直す
    /// @return 形全体の体積 [m³]
    float BuildCells(std::array<Cell, kSampleCount>& cells) const;

    float waterDensity_ = 1025.0f;
    float volume_ = 0.0f;
    float waterDrag_ = 1.0f;
    float waterAngularDrag_ = 1.0f;

    WaterQuery* query_ = nullptr;
    std::array<uint32_t, kSampleCount> points_{};
    float submergedRatio_ = 0.0f;
};
}
