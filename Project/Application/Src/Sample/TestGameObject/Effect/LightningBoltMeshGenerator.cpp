#include "pch.h"
#include "LightningBoltMeshGenerator.h"

#include "Graphics/Model/MaterialAsset.h"
#include "Math/MathCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace {
    using CoreEngine::ModelData;
    using CoreEngine::SubMeshData;
    using CoreEngine::Vector2;
    using CoreEngine::Vector3;
    using CoreEngine::Vector4;
    using CoreEngine::VertexData;

    struct PathPoint {
        Vector3 position;
        float normalizedDistance = 0.0f;
    };

    Vector3 AddVec3(const Vector3& a, const Vector3& b) {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    Vector3 SubtractVec3(const Vector3& a, const Vector3& b) {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    Vector3 MultiplyVec3(const Vector3& v, float s) {
        return { v.x * s, v.y * s, v.z * s };
    }

    float DotVec3(const Vector3& a, const Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vector3 CrossVec3(const Vector3& a, const Vector3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    }

    float LengthVec3(const Vector3& v) {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    Vector3 NormalizeVec3(const Vector3& v) {
        const float length = LengthVec3(v);
        if (length <= 1.0e-5f) {
            return { 1.0f, 0.0f, 0.0f };
        }
        return { v.x / length, v.y / length, v.z / length };
    }

    float RandomRange(std::mt19937& rng, float minValue, float maxValue) {
        std::uniform_real_distribution<float> dist(minValue, maxValue);
        return dist(rng);
    }

    VertexData MakeVertex(const Vector3& position, const Vector2& uv, const Vector3& normal, const Vector3& tangent) {
        VertexData vertex{};
        vertex.position = { position.x, position.y, position.z, 1.0f };
        vertex.texcoord = uv;
        vertex.normal = normal;
        vertex.tangent = tangent;
        return vertex;
    }

    void AppendQuad(ModelData& data,
        const Vector3& a,
        const Vector3& b,
        const Vector3& c,
        const Vector3& d,
        float v0,
        float v1,
        bool flipNormal)
    {
        const Vector3 tangent = NormalizeVec3(SubtractVec3(c, a));
        Vector3 normal = NormalizeVec3(CrossVec3(SubtractVec3(b, a), SubtractVec3(c, a)));
        if (flipNormal) {
            normal = MultiplyVec3(normal, -1.0f);
        }

        const uint32_t baseIndex = static_cast<uint32_t>(data.vertices.size());
        data.vertices.push_back(MakeVertex(a, { 0.0f, v0 }, normal, tangent));
        data.vertices.push_back(MakeVertex(b, { 1.0f, v0 }, normal, tangent));
        data.vertices.push_back(MakeVertex(c, { 0.0f, v1 }, normal, tangent));
        data.vertices.push_back(MakeVertex(d, { 1.0f, v1 }, normal, tangent));

        data.indices.push_back(static_cast<int32_t>(baseIndex + 0));
        data.indices.push_back(static_cast<int32_t>(baseIndex + 2));
        data.indices.push_back(static_cast<int32_t>(baseIndex + 1));
        data.indices.push_back(static_cast<int32_t>(baseIndex + 1));
        data.indices.push_back(static_cast<int32_t>(baseIndex + 2));
        data.indices.push_back(static_cast<int32_t>(baseIndex + 3));
    }

    std::vector<PathPoint> BuildTrunkPoints(const LightningBoltMeshGenerator::Settings& settings, std::mt19937& rng) {
        std::vector<PathPoint> points;
        points.reserve(settings.segmentCount + 1);
        points.push_back({ { 0.0f, 0.0f, 0.0f }, 0.0f });

        float lateralX = 0.0f;
        float lateralZ = 0.0f;
        float velocityX = 0.0f;
        float velocityZ = 0.0f;
        float targetX = 0.0f;
        float targetZ = 0.0f;
        std::uniform_int_distribution<uint32_t> retargetStepDist(1u, 3u);
        uint32_t nextRetargetStep = 1;
        for (uint32_t i = 1; i <= settings.segmentCount; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(settings.segmentCount);
            const float spreadBias = 0.35f + std::sin(t * 3.14159265f) * 0.95f;
            const float targetSpread = std::min(1.35f, settings.trunkJitter * (1.45f + spreadBias * 2.55f));
            if (i >= nextRetargetStep) {
                targetX = RandomRange(rng, -targetSpread, targetSpread);
                targetZ = RandomRange(rng, -targetSpread, targetSpread);
                nextRetargetStep = i + retargetStepDist(rng);
            }

            const float chaos = settings.trunkJitter * (0.70f + t * 1.35f);
            velocityX += (targetX - lateralX) * 0.95f + RandomRange(rng, -chaos, chaos);
            velocityZ += (targetZ - lateralZ) * 0.95f + RandomRange(rng, -chaos, chaos);
            velocityX *= 0.52f;
            velocityZ *= 0.52f;
            lateralX += velocityX;
            lateralZ += velocityZ;

            const float spreadClamp = std::min(1.45f, settings.trunkJitter * (1.90f + spreadBias * 2.85f));
            lateralX = std::clamp(lateralX, -spreadClamp, spreadClamp);
            lateralZ = std::clamp(lateralZ, -spreadClamp, spreadClamp);
            points.push_back({ { lateralX, -settings.boltLength * t, lateralZ }, t });
        }

        return points;
    }

    std::vector<PathPoint> BuildBranchPoints(
        std::mt19937& rng,
        const PathPoint& origin,
        float branchLength,
        float branchYaw)
    {
        std::vector<PathPoint> points;
        constexpr uint32_t kBranchSegments = 5;
        points.reserve(kBranchSegments + 1);
        points.push_back(origin);

        float x = origin.position.x;
        float y = origin.position.y;
        float z = origin.position.z;
        float currentYaw = branchYaw;
        const float lateralJitterScale = std::max(branchLength * 0.11f, 0.05f);
        for (uint32_t i = 1; i <= kBranchSegments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kBranchSegments);
            currentYaw += RandomRange(rng, -0.42f, 0.42f);
            const float dirX = std::cos(currentYaw);
            const float dirZ = std::sin(currentYaw);
            const float sideways = branchLength * (0.24f + t * 1.45f);
            x += dirX * branchLength * (0.24f + t * 0.12f) + RandomRange(rng, -lateralJitterScale, lateralJitterScale);
            z += dirZ * branchLength * (0.24f + t * 0.12f) + RandomRange(rng, -lateralJitterScale, lateralJitterScale);
            y -= branchLength * (0.12f + t * 0.08f) + RandomRange(rng, 0.005f, 0.03f);
            points.push_back({ { x + dirX * sideways, y, z + dirZ * sideways }, t });
        }

        return points;
    }

    void AppendRibbonForPath(ModelData& data, const std::vector<PathPoint>& points, float baseWidth, float tipWidth) {
        if (points.size() < 2) {
            return;
        }

        for (size_t i = 0; i + 1 < points.size(); ++i) {
            const PathPoint& p0 = points[i];
            const PathPoint& p1 = points[i + 1];
            const Vector3 segment = SubtractVec3(p1.position, p0.position);
            const Vector3 forward = NormalizeVec3(segment);
            Vector3 widthAxisA = NormalizeVec3(CrossVec3(forward, { 0.0f, 0.0f, 1.0f }));
            if (LengthVec3(widthAxisA) <= 1.0e-4f) {
                widthAxisA = NormalizeVec3(CrossVec3(forward, { 1.0f, 0.0f, 0.0f }));
            }
            Vector3 widthAxisB = NormalizeVec3(CrossVec3(forward, widthAxisA));
            if (LengthVec3(widthAxisB) <= 1.0e-4f) {
                widthAxisB = { 0.0f, 0.0f, 1.0f };
            }

            const float width0 = std::lerp(baseWidth, tipWidth, p0.normalizedDistance);
            const float width1 = std::lerp(baseWidth, tipWidth, p1.normalizedDistance);

            const Vector3 a0 = AddVec3(p0.position, MultiplyVec3(widthAxisA, width0));
            const Vector3 b0 = AddVec3(p0.position, MultiplyVec3(widthAxisA, -width0));
            const Vector3 a1 = AddVec3(p1.position, MultiplyVec3(widthAxisA, width1));
            const Vector3 b1 = AddVec3(p1.position, MultiplyVec3(widthAxisA, -width1));
            AppendQuad(data, a0, b0, a1, b1, p0.normalizedDistance, p1.normalizedDistance, false);
            AppendQuad(data, b0, a0, b1, a1, p0.normalizedDistance, p1.normalizedDistance, true);

            const Vector3 c0 = AddVec3(p0.position, MultiplyVec3(widthAxisB, width0 * 0.72f));
            const Vector3 d0 = AddVec3(p0.position, MultiplyVec3(widthAxisB, -width0 * 0.72f));
            const Vector3 c1 = AddVec3(p1.position, MultiplyVec3(widthAxisB, width1 * 0.72f));
            const Vector3 d1 = AddVec3(p1.position, MultiplyVec3(widthAxisB, -width1 * 0.72f));
            AppendQuad(data, c0, d0, c1, d1, p0.normalizedDistance, p1.normalizedDistance, false);
            AppendQuad(data, d0, c0, d1, c1, p0.normalizedDistance, p1.normalizedDistance, true);
        }
    }
}

LightningBoltMeshGenerator::LightningBoltMeshGenerator(Settings settings)
    : settings_(settings) {
}

CoreEngine::ModelData LightningBoltMeshGenerator::Generate() const {
    ModelData data;
    std::mt19937 rng(settings_.seed);

    const auto trunkPoints = BuildTrunkPoints(settings_, rng);
    AppendRibbonForPath(data, trunkPoints, settings_.baseWidth, settings_.tipWidth);

    if (trunkPoints.size() > 3) {
        for (uint32_t branchIndex = 0; branchIndex < settings_.branchCount; ++branchIndex) {
            const float branchT = (static_cast<float>(branchIndex) + 0.5f) / static_cast<float>(settings_.branchCount);
            const float originT = std::clamp(
                std::lerp(0.20f, 0.82f, branchT) + RandomRange(rng, -0.07f, 0.07f),
                0.16f,
                0.86f);
            const uint32_t originIndex = std::clamp(
                static_cast<uint32_t>(std::round(originT * static_cast<float>(trunkPoints.size() - 1))),
                2u,
                static_cast<uint32_t>(trunkPoints.size() - 2));
            const PathPoint& origin = trunkPoints[originIndex];
            const float branchLength = RandomRange(rng, settings_.branchLengthMin, settings_.branchLengthMax);
            const float branchSide = (branchIndex % 2 == 0) ? -1.0f : 1.0f;
            const float branchYaw = branchSide * RandomRange(rng, 1.05f, 2.35f) + RandomRange(rng, -0.24f, 0.24f);
            auto branchPoints = BuildBranchPoints(rng, origin, branchLength, branchYaw);
            AppendRibbonForPath(data, branchPoints, settings_.baseWidth * 0.68f, settings_.tipWidth * 0.42f);
        }
    }

    SubMeshData subMesh{};
    subMesh.name = "Lightning";
    subMesh.startIndex = 0;
    subMesh.indexCount = static_cast<uint32_t>(data.indices.size());
    subMesh.materialIndex = 0;
    data.subMeshes.push_back(subMesh);

    CoreEngine::MaterialAsset material{};
    material.name = "Lightning";
    data.materials.push_back(material);

    data.rootNode.name = "root";
    data.rootNode.localMatrix = CoreEngine::MathCore::Matrix::Identity();
    data.rootNode.transform = { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } };

    return data;
}

std::string LightningBoltMeshGenerator::GetCacheKey() const {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
        "Primitive::Lightning_%u_%u_%u_%.3f_%.3f_%.3f_%.3f_%.3f_%.3f",
        settings_.seed,
        settings_.segmentCount,
        settings_.branchCount,
        settings_.boltLength,
        settings_.trunkJitter,
        settings_.branchLengthMin,
        settings_.branchLengthMax,
        settings_.baseWidth,
        settings_.tipWidth);
    return buffer;
}
