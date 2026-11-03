#pragma once

#include <cstdint>

#include "GameObject/Component/Core/IComponent.h"
#include "Graphics/Water/Query/WaterQuery.h"
#include "Math/Vector/Vector3.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
    /// @brief オブジェクトの位置の水面を調べる
    /// @details 位置の真上・真下の水面の高さ・深さ・向きを返す。値は描かれる水面と同じで、1〜2 フレーム遅れて届く
    ///          （届くまでと水面の範囲の外では inWater が false）。調べる位置は再生中、毎フレームの最後に今の位置へ移す。
    class WaterProbeComponent : public IComponent {
    public:
        const char* GetTypeName() const override { return "WaterProbe"; }

        REFLECT_BEGIN(WaterProbeComponent, "水面の高さを調べる")
            REFLECT_READONLY_ACCESSOR("inWater", "水面の範囲の中", IsInWater)
            REFLECT_READONLY_ACCESSOR("underwater", "水の中", IsUnderwater)
            REFLECT_READONLY_ACCESSOR("surfaceHeight", "水面の高さ", GetSurfaceHeight)
            REFLECT_READONLY_ACCESSOR("depth", "水面からの深さ", GetDepth)
            REFLECT_READONLY_ACCESSOR("surfaceNormal", "水面の向き", GetSurfaceNormal)
        REFLECT_END()

        /// @brief 調べる点を足す
        void OnEnable() override;

        /// @brief 調べる点を外す
        void OnDisable() override;

        /// @brief 調べる位置を今の位置へ移す
        void LateUpdate() override;

        /// @brief 真上か真下に水面があるか（結果が届いていて、水面の範囲の中）
        bool IsInWater() const { return GetSample().inWater; }

        /// @brief 水面より下にいるか
        bool IsUnderwater() const { return GetDepth() > 0.0f; }

        /// @brief 水面の高さ（ワールド Y）[m]。水面が無ければ 0
        float GetSurfaceHeight() const { return GetSample().surfaceHeight; }

        /// @brief 水面から下へ測った深さ [m]（水面より上なら負）。水面が無ければ 0
        float GetDepth() const;

        /// @brief 水面の法線。水面が無ければ真上
        Vector3 GetSurfaceNormal() const { return GetSample().normal; }

        /// @brief 今の位置の水面
        WaterSample GetSample() const;

    private:
        WaterQuery* query_ = nullptr;
        uint32_t point_ = WaterQuery::kInvalidPoint;
    };
}
