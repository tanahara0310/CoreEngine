#pragma once

#include <array>
#include <cstdint>

#include "GameObject/Component/Core/IComponent.h"
#include "Graphics/Water/Query/WaterQuery.h"
#include "Graphics/Water/Ripple/WaterRippleFootprint.h"
#include "Physics/DisplacementCells.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
    /// @brief 動く物から水の波紋を起こす（浮力で浮く物は Buoyancy の「波紋の強さ」を使う）
    /// @details 形（兄弟のコライダー。無ければ 1 辺 1 の箱をオーナーのスケールで伸ばしたもの）の水面より下にある分を
    ///          水を押し下げているとみなし、その変化を水の波紋として入れる（WaterRippleFootprint）。
    ///          水面を進めば航跡、上下に動けば輪が広がり、水面を速く上下させた所には泡が残る。
    ///          止まっている物は何も入れない。再生中、毎フレームの最後に入れる。
    class WaterRippleEmitterComponent : public IComponent {
    public:
        const char* GetTypeName() const override { return "WaterRippleEmitter"; }

        REFLECT_BEGIN(WaterRippleEmitterComponent, "波紋を起こす")
            REFLECT_PROPERTY(strength_, "波紋の強さ", p.range = Range(0.0f, 10.0f, 0.01f),
                p.tooltip = "進む・沈む・浮き上がるときに押しのけた水の変化を、水の波紋にする倍率。0 で波紋を起こさない")
            REFLECT_PROPERTY(foamStrength_, "泡の強さ", p.range = Range(0.0f, 10.0f, 0.01f),
                p.tooltip = "速く進む・勢いよく水に入るときに、かき回した水に白い泡を残す倍率。0 で泡を残さない。泡は主の水を FFT の海で描き、泡を入れているときに出る")
            REFLECT_READONLY_ACCESSOR("inWater", "水に触れている", IsInWater)
            REFLECT_READONLY_ACCESSOR("displacedVolume", "押しのけている水の体積", GetDisplacedVolume)
        REFLECT_END()

        /// @brief 水面の高さを調べる点を足す
        void OnEnable() override;

        /// @brief 水面の高さを調べる点を外す
        void OnDisable() override;

        /// @brief 押しのけた水の変化を水の波紋と泡の源として入れる
        void LateUpdate() override;

        /// @brief 形の一部が水面より下にあるか
        bool IsInWater() const { return footprint_.GetDisplacedVolume() > 0.0f; }

        /// @brief 形のうち、波紋を除いた水面より下にある体積 [m³]（倍率を掛ける前）
        float GetDisplacedVolume() const { return footprint_.GetDisplacedVolume(); }

        float GetStrength() const { return strength_; }
        void SetStrength(float strength) { strength_ = strength; }

        float GetFoamStrength() const { return foamStrength_; }
        void SetFoamStrength(float strength) { foamStrength_ = strength; }

    private:
        float strength_ = 1.0f;
        float foamStrength_ = 1.0f;

        WaterQuery* query_ = nullptr;
        std::array<uint32_t, kDisplacementCellCount> points_{};
        WaterRippleFootprint footprint_{};
    };
}
