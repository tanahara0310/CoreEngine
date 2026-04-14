#include "SphereMeshGenerator.h"
#include "Math/MathCore.h"
#include <cmath>
#include <cstdio>

namespace CoreEngine
{
    SphereMeshGenerator::SphereMeshGenerator(float radius, uint32_t slices, uint32_t stacks)
        : radius_(radius), slices_(slices), stacks_(stacks)
    {
    }

    ModelData SphereMeshGenerator::Generate() const
    {
        ModelData data;

        constexpr float kPi = 3.14159265358979f;

        // 頂点生成
        data.vertices.reserve((stacks_ + 1) * (slices_ + 1));
        for (uint32_t stack = 0; stack <= stacks_; ++stack) {
            float phi = kPi * static_cast<float>(stack) / stacks_;
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);

            for (uint32_t slice = 0; slice <= slices_; ++slice) {
                float theta = 2.0f * kPi * static_cast<float>(slice) / slices_;
                float sinTheta = std::sin(theta);
                float cosTheta = std::cos(theta);

                Vector3 normal = { sinPhi * cosTheta, cosPhi, sinPhi * sinTheta };

                VertexData vertex{};
                vertex.position = { normal.x * radius_, normal.y * radius_, normal.z * radius_, 1.0f };
                vertex.texcoord = { static_cast<float>(slice) / slices_, static_cast<float>(stack) / stacks_ };
                vertex.normal   = normal;

                // タンジェント（経度方向の接線）
                vertex.tangent = { -sinTheta, 0.0f, cosTheta };

                data.vertices.push_back(vertex);
            }
        }

        // インデックス生成
        data.indices.reserve(stacks_ * slices_ * 6);
        for (uint32_t stack = 0; stack < stacks_; ++stack) {
            for (uint32_t slice = 0; slice < slices_; ++slice) {
                uint32_t current = stack * (slices_ + 1) + slice;
                uint32_t next    = current + slices_ + 1;

                data.indices.push_back(static_cast<int32_t>(current));
                data.indices.push_back(static_cast<int32_t>(current + 1));
                data.indices.push_back(static_cast<int32_t>(next));

                data.indices.push_back(static_cast<int32_t>(current + 1));
                data.indices.push_back(static_cast<int32_t>(next + 1));
                data.indices.push_back(static_cast<int32_t>(next));
            }
        }

        // サブメッシュ
        SubMeshData subMesh{};
        subMesh.name = "Sphere";
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

    std::string SphereMeshGenerator::GetCacheKey() const
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Primitive::Sphere_%.2f_%u_%u",
                 radius_, slices_, stacks_);
        return buf;
    }
}
