#pragma once

#include "Graphics/Primitive/IPrimitiveMeshGenerator.h"

#include <cstdint>
#include <string>

namespace CoreEngine
{
    /// @brief 水面のメッシュ（遠くまで覆う粗い格子と、カメラの周りに置く岸の細かい格子を 1 本にまとめたもの）
    /// @details 粗い格子は PlaneMeshGenerator と同じ並び（各マスの対角は (1,0)-(0,1)）。
    ///          岸の格子は同じ並びの kShoreSubdivisions 四方の格子で、頂点の位置は原点に置き、
    ///          animData に (格子の中の u, 格子の中の v, 1, 0) を入れる。
    ///          置く場所は頂点シェーダ（FFTWater.VS）が毎フレームの範囲（WaterFrameConstants）から決める
    class WaterSurfaceMeshGenerator : public IPrimitiveMeshGenerator {
    public:
        /// @brief 岸の格子の一辺の分割数
        static constexpr uint32_t kShoreSubdivisions = 512;

        /// @param size 粗い格子の一辺の長さ（ローカル）
        /// @param subdivisions 粗い格子の一辺の分割数
        WaterSurfaceMeshGenerator(float size, uint32_t subdivisions);

        ModelData Generate() const override;
        std::string GetCacheKey() const override;
        const char* GetDisplayName() const override { return "水面"; }

        /// @brief 岸の格子は頂点を原点に置いて頂点シェーダで動かすので、LOD は作らない
        bool UseLod() const override { return false; }

    private:
        float size_ = 1.0f;
        uint32_t subdivisions_ = 1;
    };
}
