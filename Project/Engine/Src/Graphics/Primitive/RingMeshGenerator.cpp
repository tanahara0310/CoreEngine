#include "pch.h"
#include "RingMeshGenerator.h"
#include "Math/MathCore.h"
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

            // ① 外周 current
            VertexData v0{};
            v0.position = { -sin * outerRadius_, cos * outerRadius_, 0.0f, 1.0f };
            v0.texcoord = { u, 0.0f };
            v0.normal = { 0.0f, 0.0f, 1.0f };
            v0.tangent = { 1.0f, 0.0f, 0.0f };

            // ② 外周 next
            VertexData v1{};
            v1.position = { -sinNext * outerRadius_, cosNext * outerRadius_, 0.0f, 1.0f };
            v1.texcoord = { uNext, 0.0f };
            v1.normal = { 0.0f, 0.0f, 1.0f };
            v1.tangent = { 1.0f, 0.0f, 0.0f };

            // ③ 内周 current
            VertexData v2{};
            v2.position = { -sin * innerRadius_, cos * innerRadius_, 0.0f, 1.0f };
            v2.texcoord = { u, 1.0f };
            v2.normal = { 0.0f, 0.0f, 1.0f };
            v2.tangent = { 1.0f, 0.0f, 0.0f };

            // ④ 内周 next
            VertexData v3{};
            v3.position = { -sinNext * innerRadius_, cosNext * innerRadius_, 0.0f, 1.0f };
            v3.texcoord = { uNext, 1.0f };
            v3.normal = { 0.0f, 0.0f, 1.0f };
            v3.tangent = { 1.0f, 0.0f, 0.0f };

            data.vertices.push_back(v0); // base + 0 : ①
            data.vertices.push_back(v1); // base + 1 : ②
            data.vertices.push_back(v2); // base + 2 : ③
            data.vertices.push_back(v3); // base + 3 : ④

            // 三角形 1: ①③② (時計回り・表面+Z向き)
            data.indices.push_back(static_cast<int32_t>(base + 0));
            data.indices.push_back(static_cast<int32_t>(base + 2));
            data.indices.push_back(static_cast<int32_t>(base + 1));

            // 三角形 2: ②③④ (時計回り・表面+Z向き)
            data.indices.push_back(static_cast<int32_t>(base + 1));
            data.indices.push_back(static_cast<int32_t>(base + 2));
            data.indices.push_back(static_cast<int32_t>(base + 3));
        }

        // サブメッシュ
        SubMeshData subMesh{};
        subMesh.name = "Ring";
        subMesh.startIndex = 0;
        subMesh.indexCount = static_cast<uint32_t>(data.indices.size());
        subMesh.materialIndex = 0;
        data.subMeshes.push_back(subMesh);

        // デフォルトマテリアル
        MaterialAsset mat{};
        mat.name = "Default";
        data.materials.push_back(mat);

        // ルートノード
        data.rootNode.name = "root";
        data.rootNode.localMatrix = MathCore::Matrix::Identity();
        data.rootNode.transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };

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
