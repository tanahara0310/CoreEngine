#include "pch.h"
#include "CylinderMeshGenerator.h"

#include "Math/MathCore.h"

#include <cmath>
#include <cstdio>
#include <numbers>

namespace CoreEngine
{
    CylinderMeshGenerator::CylinderMeshGenerator(float topRadius, float bottomRadius,
        float height, uint32_t divisions)
        : topRadius_(topRadius)
        , bottomRadius_(bottomRadius)
        , height_(height)
        , divisions_(std::max(3u, divisions))
    {
    }

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

            VertexData v0{};
            v0.position = { -sin * topRadius_, topY, cos * topRadius_, 1.0f };
            v0.texcoord = { u, 1.0f };
            v0.normal = normal;
            v0.tangent = tangent;

            VertexData v1{};
            v1.position = { -sinNext * topRadius_, topY, cosNext * topRadius_, 1.0f };
            v1.texcoord = { uNext, 1.0f };
            v1.normal = normalNext;
            v1.tangent = tangentNext;

            VertexData v2{};
            v2.position = { -sin * bottomRadius_, bottomY, cos * bottomRadius_, 1.0f };
            v2.texcoord = { u, 0.0f };
            v2.normal = normal;
            v2.tangent = tangent;

            VertexData v3{};
            v3.position = { -sinNext * bottomRadius_, bottomY, cosNext * bottomRadius_, 1.0f };
            v3.texcoord = { uNext, 0.0f };
            v3.normal = normalNext;
            v3.tangent = tangentNext;

            data.vertices.push_back(v0);
            data.vertices.push_back(v1);
            data.vertices.push_back(v2);
            data.vertices.push_back(v3);

            data.indices.push_back(static_cast<int32_t>(baseIndex + 0));
            data.indices.push_back(static_cast<int32_t>(baseIndex + 1));
            data.indices.push_back(static_cast<int32_t>(baseIndex + 2));

            data.indices.push_back(static_cast<int32_t>(baseIndex + 2));
            data.indices.push_back(static_cast<int32_t>(baseIndex + 1));
            data.indices.push_back(static_cast<int32_t>(baseIndex + 3));

        }

        // 上面キャップ
        for (uint32_t index = 0; index < divisions_; ++index) {
            const float radian = static_cast<float>(index) * radianPerDivide;
            const float nextRadian = static_cast<float>(index + 1) * radianPerDivide;

            const float sin = std::sin(radian);
            const float cos = std::cos(radian);
            const float sinNext = std::sin(nextRadian);
            const float cosNext = std::cos(nextRadian);

            const float x = -sin * topRadius_;
            const float z = cos * topRadius_;
            const float xNext = -sinNext * topRadius_;
            const float zNext = cosNext * topRadius_;

            const uint32_t baseIndex = static_cast<uint32_t>(data.vertices.size());

            VertexData v0{};
            v0.position = { x, topY, z, 1.0f };
            v0.texcoord = { 0.5f, 0.5f };
            v0.normal = { 0.0f, 1.0f, 0.0f };
            v0.tangent = { 1.0f, 0.0f, 0.0f };

            VertexData v1{};
            v1.position = { 0.0f, topY, 0.0f, 1.0f };
            v1.texcoord = { 0.5f, 0.5f };
            v1.normal = { 0.0f, 1.0f, 0.0f };
            v1.tangent = { 1.0f, 0.0f, 0.0f };

            VertexData v2{};
            v2.position = { xNext, topY, zNext, 1.0f };
            v2.texcoord = { 0.5f, 0.5f };
            v2.normal = { 0.0f, 1.0f, 0.0f };
            v2.tangent = { 1.0f, 0.0f, 0.0f };

            data.vertices.push_back(v0);
            data.vertices.push_back(v1);
            data.vertices.push_back(v2);

            data.indices.push_back(static_cast<int32_t>(baseIndex + 0));
            data.indices.push_back(static_cast<int32_t>(baseIndex + 1));
            data.indices.push_back(static_cast<int32_t>(baseIndex + 2));

        }

        // 下面キャップ
        for (uint32_t index = 0; index < divisions_; ++index) {
            const float radian = static_cast<float>(index) * radianPerDivide;
            const float nextRadian = static_cast<float>(index + 1) * radianPerDivide;

            const float sin = std::sin(radian);
            const float cos = std::cos(radian);
            const float sinNext = std::sin(nextRadian);
            const float cosNext = std::cos(nextRadian);

            const float x = -sin * bottomRadius_;
            const float z = cos * bottomRadius_;
            const float xNext = -sinNext * bottomRadius_;
            const float zNext = cosNext * bottomRadius_;

            const uint32_t baseIndex = static_cast<uint32_t>(data.vertices.size());

            VertexData v0{};
            v0.position = { x, bottomY, z, 1.0f };
            v0.texcoord = { 0.5f, 0.5f };
            v0.normal = { 0.0f, -1.0f, 0.0f };
            v0.tangent = { 1.0f, 0.0f, 0.0f };

            VertexData v1{};
            v1.position = { xNext, bottomY, zNext, 1.0f };
            v1.texcoord = { 0.5f, 0.5f };
            v1.normal = { 0.0f, -1.0f, 0.0f };
            v1.tangent = { 1.0f, 0.0f, 0.0f };

            VertexData v2{};
            v2.position = { 0.0f, bottomY, 0.0f, 1.0f };
            v2.texcoord = { 0.5f, 0.5f };
            v2.normal = { 0.0f, -1.0f, 0.0f };
            v2.tangent = { 1.0f, 0.0f, 0.0f };

            data.vertices.push_back(v0);
            data.vertices.push_back(v1);
            data.vertices.push_back(v2);

            data.indices.push_back(static_cast<int32_t>(baseIndex + 0));
            data.indices.push_back(static_cast<int32_t>(baseIndex + 1));
            data.indices.push_back(static_cast<int32_t>(baseIndex + 2));

        }

        SubMeshData subMesh{};
        subMesh.name = "Cylinder";
        subMesh.startIndex = 0;
        subMesh.indexCount = static_cast<uint32_t>(data.indices.size());
        subMesh.materialIndex = 0;
        data.subMeshes.push_back(subMesh);

        MaterialAsset mat{};
        mat.name = "Default";
        data.materials.push_back(mat);

        data.rootNode.name = "root";
        data.rootNode.localMatrix = MathCore::Matrix::Identity();
        data.rootNode.transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };

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
