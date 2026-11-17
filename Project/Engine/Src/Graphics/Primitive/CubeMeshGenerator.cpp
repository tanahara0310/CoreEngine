#include "pch.h"
#include "CubeMeshGenerator.h"
#include "Graphics/Primitive/PrimitiveMeshUtil.h"
#include <cstdio>

namespace CoreEngine
{
    CubeMeshGenerator::CubeMeshGenerator(float size)
        : width_(size), height_(size), depth_(size)
    {
    }

    CubeMeshGenerator::CubeMeshGenerator(float width, float height, float depth)
        : width_(width), height_(height), depth_(depth)
    {
    }

    ModelData CubeMeshGenerator::Generate() const
    {
        ModelData data;

        const float hw = width_  * 0.5f;
        const float hh = height_ * 0.5f;
        const float hd = depth_  * 0.5f;

        // 面ごとに4頂点 × 6面 = 24頂点（法線・UVが面ごとに異なるため共有しない）
        struct FaceDesc {
            Vector3 normal;
            Vector3 tangent;
            Vector4 positions[4];
            Vector2 texcoords[4];
        };

        FaceDesc faces[6] = {
            // +Y（上面）
            { {0,1,0}, {1,0,0},
              {{ -hw, hh, -hd, 1 }, { hw, hh, -hd, 1 }, { hw, hh, hd, 1 }, { -hw, hh, hd, 1 }},
              {{ 0,0 }, { 1,0 }, { 1,1 }, { 0,1 }} },
            // -Y（底面）
            { {0,-1,0}, {1,0,0},
              {{ -hw, -hh, hd, 1 }, { hw, -hh, hd, 1 }, { hw, -hh, -hd, 1 }, { -hw, -hh, -hd, 1 }},
              {{ 0,0 }, { 1,0 }, { 1,1 }, { 0,1 }} },
            // +Z（前面）
            { {0,0,1}, {1,0,0},
              {{ -hw, -hh, hd, 1 }, { -hw, hh, hd, 1 }, { hw, hh, hd, 1 }, { hw, -hh, hd, 1 }},
              {{ 0,1 }, { 0,0 }, { 1,0 }, { 1,1 }} },
            // -Z（背面）
            { {0,0,-1}, {-1,0,0},
              {{ hw, -hh, -hd, 1 }, { hw, hh, -hd, 1 }, { -hw, hh, -hd, 1 }, { -hw, -hh, -hd, 1 }},
              {{ 0,1 }, { 0,0 }, { 1,0 }, { 1,1 }} },
            // +X（右面）
            { {1,0,0}, {0,0,1},
              {{ hw, -hh, hd, 1 }, { hw, hh, hd, 1 }, { hw, hh, -hd, 1 }, { hw, -hh, -hd, 1 }},
              {{ 0,1 }, { 0,0 }, { 1,0 }, { 1,1 }} },
            // -X（左面）
            { {-1,0,0}, {0,0,-1},
              {{ -hw, -hh, -hd, 1 }, { -hw, hh, -hd, 1 }, { -hw, hh, hd, 1 }, { -hw, -hh, hd, 1 }},
              {{ 0,1 }, { 0,0 }, { 1,0 }, { 1,1 }} },
        };

        data.vertices.reserve(24);
        data.indices.reserve(36);

        for (int f = 0; f < 6; ++f) {
            uint32_t baseIndex = static_cast<uint32_t>(data.vertices.size());

            for (int v = 0; v < 4; ++v) {
                data.vertices.push_back(PrimitiveMeshUtil::MakeVertex(
                    faces[f].positions[v], faces[f].texcoords[v], faces[f].normal, faces[f].tangent));
            }

            // 2三角形（反時計回り＝外向き法線）
            PrimitiveMeshUtil::PushTriangle(data, baseIndex, baseIndex + 2, baseIndex + 1);
            PrimitiveMeshUtil::PushTriangle(data, baseIndex, baseIndex + 3, baseIndex + 2);
        }

        PrimitiveMeshUtil::FinishSingleMesh(data, "Cube");
        return data;
    }

    std::string CubeMeshGenerator::GetCacheKey() const
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Primitive::Cube_%.2f_%.2f_%.2f",
                 width_, height_, depth_);
        return buf;
    }
}
