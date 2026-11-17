#include "pch.h"
#include "RingMeshGenerator.h"
#include "Graphics/Primitive/PrimitiveMeshUtil.h"
#include <cmath>
#include <numbers>
#include <cstdio>

namespace CoreEngine
{
    RingMeshGenerator::RingMeshGenerator(float outerRadius, float innerRadius, uint32_t divisions)
        : outerRadius_(outerRadius), innerRadius_(innerRadius), divisions_(divisions)
    {
    }

    ModelData RingMeshGenerator::Generate() const
    {
        ModelData data;

        // 各分割ごとに 4 頂点（①②③④）、6 インデックス（2 三角形）を生成する
        // XY 平面上に時計回りに配置
        // ① 外周(index)    ② 外周(index+1)
        // ③ 内周(index)    ④ 内周(index+1)
        const float radianPerDivide =
            2.0f * static_cast<float>(std::numbers::pi) / static_cast<float>(divisions_);

        data.vertices.reserve(divisions_ * 4);
        data.indices.reserve(divisions_ * 6);

        for (uint32_t index = 0; index < divisions_; ++index)
        {
            const float sin = std::sin(static_cast<float>(index) * radianPerDivide);
            const float cos = std::cos(static_cast<float>(index) * radianPerDivide);
            const float sinNext = std::sin(static_cast<float>(index + 1) * radianPerDivide);
            const float cosNext = std::cos(static_cast<float>(index + 1) * radianPerDivide);
            const float u = static_cast<float>(index) / static_cast<float>(divisions_);
            const float uNext = static_cast<float>(index + 1) / static_cast<float>(divisions_);

            const uint32_t base = index * 4;

            const Vector3 normal = { 0.0f, 0.0f, 1.0f };
            const Vector3 tangent = { 1.0f, 0.0f, 0.0f };

            // base + 0 : ① 外周 current
            data.vertices.push_back(PrimitiveMeshUtil::MakeVertex(
                { -sin * outerRadius_, cos * outerRadius_, 0.0f, 1.0f }, { u, 0.0f }, normal, tangent));
            // base + 1 : ② 外周 next
            data.vertices.push_back(PrimitiveMeshUtil::MakeVertex(
                { -sinNext * outerRadius_, cosNext * outerRadius_, 0.0f, 1.0f }, { uNext, 0.0f }, normal, tangent));
            // base + 2 : ③ 内周 current
            data.vertices.push_back(PrimitiveMeshUtil::MakeVertex(
                { -sin * innerRadius_, cos * innerRadius_, 0.0f, 1.0f }, { u, 1.0f }, normal, tangent));
            // base + 3 : ④ 内周 next
            data.vertices.push_back(PrimitiveMeshUtil::MakeVertex(
                { -sinNext * innerRadius_, cosNext * innerRadius_, 0.0f, 1.0f }, { uNext, 1.0f }, normal, tangent));

            // 三角形 1: ①③② (時計回り・表面+Z向き)
            PrimitiveMeshUtil::PushTriangle(data, base + 0, base + 2, base + 1);
            // 三角形 2: ②③④ (時計回り・表面+Z向き)
            PrimitiveMeshUtil::PushTriangle(data, base + 1, base + 2, base + 3);
        }

        PrimitiveMeshUtil::FinishSingleMesh(data, "Ring");
        return data;
    }

    std::string RingMeshGenerator::GetCacheKey() const
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Primitive::Ring_%.2f_%.2f_%u",
            outerRadius_, innerRadius_, divisions_);
        return buf;
    }
}
