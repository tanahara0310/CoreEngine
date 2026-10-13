#include "pch.h"
#include "WaterSurfaceMeshGenerator.h"

#include "Graphics/Primitive/PlaneMeshGenerator.h"

#include <cstdio>

namespace CoreEngine
{
    WaterSurfaceMeshGenerator::WaterSurfaceMeshGenerator(float size, uint32_t subdivisions)
        : size_(size), subdivisions_(subdivisions)
    {
    }

    ModelData WaterSurfaceMeshGenerator::Generate() const
    {
        ModelData data = PlaneMeshGenerator(size_, size_, subdivisions_, subdivisions_).Generate();

        // 岸の格子の頂点（位置は頂点シェーダが決める）
        const uint32_t baseVertex = static_cast<uint32_t>(data.vertices.size());
        const uint32_t verts = kShoreSubdivisions + 1;
        data.vertices.reserve(baseVertex + verts * verts);
        for (uint32_t z = 0; z < verts; ++z) {
            for (uint32_t x = 0; x < verts; ++x) {
                VertexData vertex{};
                vertex.position = { 0.0f, 0.0f, 0.0f, 1.0f };
                vertex.normal = { 0.0f, 1.0f, 0.0f };
                vertex.tangent = { 1.0f, 0.0f, 0.0f };
                vertex.animData = {
                    static_cast<float>(x) / kShoreSubdivisions,
                    static_cast<float>(z) / kShoreSubdivisions,
                    1.0f,
                    0.0f };
                data.vertices.push_back(vertex);
            }
        }

        // 粗い格子と同じ並びの三角形（各マスの対角は (1,0)-(0,1)）
        data.indices.reserve(data.indices.size() + kShoreSubdivisions * kShoreSubdivisions * 6);
        for (uint32_t z = 0; z < kShoreSubdivisions; ++z) {
            for (uint32_t x = 0; x < kShoreSubdivisions; ++x) {
                const uint32_t topLeft = baseVertex + z * verts + x;
                const uint32_t topRight = topLeft + 1;
                const uint32_t bottomLeft = topLeft + verts;
                const uint32_t bottomRight = bottomLeft + 1;

                data.indices.push_back(static_cast<int32_t>(topLeft));
                data.indices.push_back(static_cast<int32_t>(bottomLeft));
                data.indices.push_back(static_cast<int32_t>(topRight));

                data.indices.push_back(static_cast<int32_t>(topRight));
                data.indices.push_back(static_cast<int32_t>(bottomLeft));
                data.indices.push_back(static_cast<int32_t>(bottomRight));
            }
        }

        // 1 回の描画で両方の格子を描く
        data.subMeshes.front().name = "WaterSurface";
        data.subMeshes.front().indexCount = static_cast<uint32_t>(data.indices.size());
        return data;
    }

    std::string WaterSurfaceMeshGenerator::GetCacheKey() const
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Primitive::WaterSurface_%.2f_%u_%u",
                 size_, subdivisions_, kShoreSubdivisions);
        return buf;
    }
}
