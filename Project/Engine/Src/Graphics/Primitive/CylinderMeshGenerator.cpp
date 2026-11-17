#include "pch.h"
#include "CylinderMeshGenerator.h"

#include "Graphics/Primitive/PrimitiveMeshUtil.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace CoreEngine
{
    namespace
    {
        /// @brief フタ 1 枚分の三角形を分割ごとに積む
        /// @param y フタの高さ
        /// @param radius フタの半径
        /// @param facingUp true なら上向きのフタ（縁・中心・次の縁の順）、false なら下向きのフタ（縁・次の縁・中心の順）
        void AppendCap(ModelData& data, uint32_t divisions, float radianPerDivide,
            float y, float radius, bool facingUp)
        {
            const Vector3 normal = { 0.0f, facingUp ? 1.0f : -1.0f, 0.0f };
            const Vector3 tangent = { 1.0f, 0.0f, 0.0f };
            const Vector2 texcoord = { 0.5f, 0.5f };

            for (uint32_t index = 0; index < divisions; ++index) {
                const float radian = static_cast<float>(index) * radianPerDivide;
                const float nextRadian = static_cast<float>(index + 1) * radianPerDivide;

                const float sin = std::sin(radian);
                const float cos = std::cos(radian);
                const float sinNext = std::sin(nextRadian);
                const float cosNext = std::cos(nextRadian);

                const VertexData rim = PrimitiveMeshUtil::MakeVertex(
                    { -sin * radius, y, cos * radius, 1.0f }, texcoord, normal, tangent);
                const VertexData rimNext = PrimitiveMeshUtil::MakeVertex(
                    { -sinNext * radius, y, cosNext * radius, 1.0f }, texcoord, normal, tangent);
                const VertexData center = PrimitiveMeshUtil::MakeVertex(
                    { 0.0f, y, 0.0f, 1.0f }, texcoord, normal, tangent);

                const uint32_t baseIndex = static_cast<uint32_t>(data.vertices.size());

                data.vertices.push_back(rim);
                data.vertices.push_back(facingUp ? center : rimNext);
                data.vertices.push_back(facingUp ? rimNext : center);

                PrimitiveMeshUtil::PushTriangle(data, baseIndex + 0, baseIndex + 1, baseIndex + 2);
            }
        }
    }

    CylinderMeshGenerator::CylinderMeshGenerator(float topRadius, float bottomRadius,
        float height, uint32_t divisions)
        : topRadius_(topRadius)
        , bottomRadius_(bottomRadius)
        , height_(height)
        , divisions_(std::max(3u, divisions))
    {
    }

    // 側面 → 上フタ → 下フタの順に三角形を積む。
    // フタは側面と法線が違うので頂点を共有せず、分割ごとに専用の頂点を作る
    ModelData CylinderMeshGenerator::Generate() const
    {
        ModelData data;

        const float topY = height_;
        const float bottomY = 0.0f;
        const float radianPerDivide =
            2.0f * static_cast<float>(std::numbers::pi) / static_cast<float>(divisions_);

        data.vertices.reserve(divisions_ * 10);
        data.indices.reserve(divisions_ * 12);

        // 側面
        for (uint32_t index = 0; index < divisions_; ++index) {
            const float radian = static_cast<float>(index) * radianPerDivide;
            const float nextRadian = static_cast<float>(index + 1) * radianPerDivide;

            const float sin = std::sin(radian);
            const float cos = std::cos(radian);
            const float sinNext = std::sin(nextRadian);
            const float cosNext = std::cos(nextRadian);

            const float u = static_cast<float>(index) / static_cast<float>(divisions_);
            const float uNext = static_cast<float>(index + 1) / static_cast<float>(divisions_);

            const Vector3 normal = { -sin, 0.0f, cos };
            const Vector3 normalNext = { -sinNext, 0.0f, cosNext };
            const Vector3 tangent = { -cos, 0.0f, -sin };
            const Vector3 tangentNext = { -cosNext, 0.0f, -sinNext };

            const uint32_t baseIndex = static_cast<uint32_t>(data.vertices.size());

            data.vertices.push_back(PrimitiveMeshUtil::MakeVertex(
                { -sin * topRadius_, topY, cos * topRadius_, 1.0f }, { u, 1.0f }, normal, tangent));
            data.vertices.push_back(PrimitiveMeshUtil::MakeVertex(
                { -sinNext * topRadius_, topY, cosNext * topRadius_, 1.0f }, { uNext, 1.0f }, normalNext, tangentNext));
            data.vertices.push_back(PrimitiveMeshUtil::MakeVertex(
                { -sin * bottomRadius_, bottomY, cos * bottomRadius_, 1.0f }, { u, 0.0f }, normal, tangent));
            data.vertices.push_back(PrimitiveMeshUtil::MakeVertex(
                { -sinNext * bottomRadius_, bottomY, cosNext * bottomRadius_, 1.0f }, { uNext, 0.0f }, normalNext, tangentNext));

            PrimitiveMeshUtil::PushTriangle(data, baseIndex + 0, baseIndex + 1, baseIndex + 2);
            PrimitiveMeshUtil::PushTriangle(data, baseIndex + 2, baseIndex + 1, baseIndex + 3);
        }

        // 上面キャップ
        AppendCap(data, divisions_, radianPerDivide, topY, topRadius_, true);

        // 下面キャップ
        AppendCap(data, divisions_, radianPerDivide, bottomY, bottomRadius_, false);

        PrimitiveMeshUtil::FinishSingleMesh(data, "Cylinder");
        return data;
    }

    std::string CylinderMeshGenerator::GetCacheKey() const
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "Primitive::Cylinder_v2_%.2f_%.2f_%.2f_%u",
            topRadius_, bottomRadius_, height_, divisions_);
        return buf;
    }
}
