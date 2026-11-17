#include "pch.h"
#include "SphereMeshGenerator.h"
#include "Graphics/Primitive/PrimitiveMeshUtil.h"
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

        constexpr float kPi = MathCore::Constants::kPi;

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

                // タンジェントは経度方向の接線
                data.vertices.push_back(PrimitiveMeshUtil::MakeVertex(
                    { normal.x * radius_, normal.y * radius_, normal.z * radius_, 1.0f },
                    { static_cast<float>(slice) / slices_, static_cast<float>(stack) / stacks_ },
                    normal,
                    { -sinTheta, 0.0f, cosTheta }));
            }
        }

        // インデックス生成
        data.indices.reserve(stacks_ * slices_ * 6);
        for (uint32_t stack = 0; stack < stacks_; ++stack) {
            for (uint32_t slice = 0; slice < slices_; ++slice) {
                uint32_t current = stack * (slices_ + 1) + slice;
                uint32_t next    = current + slices_ + 1;

                PrimitiveMeshUtil::PushTriangle(data, current, current + 1, next);
                PrimitiveMeshUtil::PushTriangle(data, current + 1, next + 1, next);
            }
        }

        PrimitiveMeshUtil::FinishSingleMesh(data, "Sphere");
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
