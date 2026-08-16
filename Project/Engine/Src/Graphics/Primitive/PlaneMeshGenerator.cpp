#include "pch.h"
#include "PlaneMeshGenerator.h"
#include "Math/MathCore.h"
#include <cstdio>

namespace CoreEngine
{
    PlaneMeshGenerator::PlaneMeshGenerator(float width, float depth,
                                           uint32_t subdivisionsX, uint32_t subdivisionsZ,
                                           float uvTiling)
        : width_(width), depth_(depth)
        , subdivisionsX_(subdivisionsX), subdivisionsZ_(subdivisionsZ)
        , uvTiling_(uvTiling)
    {
    }

    ModelData PlaneMeshGenerator::Generate() const
    {
        ModelData data;

        const uint32_t vertsX = subdivisionsX_ + 1;
        const uint32_t vertsZ = subdivisionsZ_ + 1;
        const float halfW = width_ * 0.5f;
        const float halfD = depth_ * 0.5f;

        // 頂点生成
        data.vertices.reserve(vertsX * vertsZ);
        for (uint32_t z = 0; z < vertsZ; ++z) {
            for (uint32_t x = 0; x < vertsX; ++x) {
                float u = static_cast<float>(x) / subdivisionsX_;
                float v = static_cast<float>(z) / subdivisionsZ_;

                VertexData vertex{};
                vertex.position = { -halfW + u * width_, 0.0f, -halfD + v * depth_, 1.0f };
                vertex.texcoord = { u * uvTiling_, (1.0f - v) * uvTiling_ };
                vertex.normal   = { 0.0f, 1.0f, 0.0f };
                vertex.tangent  = { 1.0f, 0.0f, 0.0f };
                data.vertices.push_back(vertex);
            }
        }

        // インデックス生成
        data.indices.reserve(subdivisionsX_ * subdivisionsZ_ * 6);
        for (uint32_t z = 0; z < subdivisionsZ_; ++z) {
            for (uint32_t x = 0; x < subdivisionsX_; ++x) {
                uint32_t topLeft     = z * vertsX + x;
                uint32_t topRight    = topLeft + 1;
                uint32_t bottomLeft  = topLeft + vertsX;
                uint32_t bottomRight = bottomLeft + 1;

                data.indices.push_back(static_cast<int32_t>(topLeft));
                data.indices.push_back(static_cast<int32_t>(bottomLeft));
                data.indices.push_back(static_cast<int32_t>(topRight));

                data.indices.push_back(static_cast<int32_t>(topRight));
                data.indices.push_back(static_cast<int32_t>(bottomLeft));
                data.indices.push_back(static_cast<int32_t>(bottomRight));
            }
        }

        // サブメッシュ
        SubMeshData subMesh{};
        subMesh.name = "Plane";
        subMesh.startIndex = 0;
        subMesh.indexCount = static_cast<uint32_t>(data.indices.size());
        subMesh.materialIndex = 0;
        data.subMeshes.push_back(subMesh);

        // デフォルトマテリアル
        MaterialAsset mat{};
        mat.name = "Default";
        data.materials.push_back(mat);

        // ルートノード（単位行列）
        data.rootNode.name = "root";
        data.rootNode.localMatrix = MathCore::Matrix::Identity();
        data.rootNode.transform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };

        return data;
    }

    std::string PlaneMeshGenerator::GetCacheKey() const
    {
        char buf[128];
        // uvTiling もキーに含める（同寸法でもタイリング違いは別メッシュ）
        snprintf(buf, sizeof(buf), "Primitive::Plane_%.2f_%.2f_%u_%u_%.2f",
                 width_, depth_, subdivisionsX_, subdivisionsZ_, uvTiling_);
        return buf;
    }
}