#include "pch.h"
#include "Math/Geometry/TriangleMesh.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <utility>

namespace CoreEngine
{
namespace Geometry
{
    namespace {
        /// @brief 葉に入れる三角形の数の上限
        constexpr uint32_t kLeafSize = 4;

        /// @brief つぶれた三角形とみなす面積（全体の対角線の 2 乗に対する割合）
        constexpr float kDegenerateAreaRatio = 1e-10f;

        /// @brief 同じ位置にまとめるための格子の座標
        struct WeldKey {
            int64_t x = 0;
            int64_t y = 0;
            int64_t z = 0;
            uint32_t source = 0;
        };

        /// @brief 縁を探すための並び
        struct EdgeEntry {
            uint64_t key = 0;       ///< 小さい頂点の番号と大きい頂点の番号
            uint32_t triangle = 0;
            uint32_t edge = 0;
        };

        Vector3 Min3(const Vector3& a, const Vector3& b)
        {
            return { (std::min)(a.x, b.x), (std::min)(a.y, b.y), (std::min)(a.z, b.z) };
        }

        Vector3 Max3(const Vector3& a, const Vector3& b)
        {
            return { (std::max)(a.x, b.x), (std::max)(a.y, b.y), (std::max)(a.z, b.z) };
        }

        float Component(const Vector3& v, int axis)
        {
            return (axis == 0) ? v.x : ((axis == 1) ? v.y : v.z);
        }

        /// @brief 縁の 2 頂点から並べ替えの鍵を作る（向きによらず同じ鍵）
        uint64_t MakeEdgeKey(uint32_t a, uint32_t b)
        {
            const uint32_t low = (std::min)(a, b);
            const uint32_t high = (std::max)(a, b);
            return (static_cast<uint64_t>(low) << 32) | high;
        }

        /// @brief 同じ位置の頂点を 1 つにまとめる
        /// @param outPositions まとめた後の位置
        /// @return 元の頂点の番号からまとめた後の番号への表
        std::vector<uint32_t> WeldPositions(const std::vector<Vector3>& positions, float step,
                                            std::vector<Vector3>& outPositions)
        {
            const size_t count = positions.size();
            std::vector<WeldKey> keys(count);
            for (size_t i = 0; i < count; ++i) {
                const Vector3& p = positions[i];
                keys[i].x = static_cast<int64_t>(std::llround(p.x / step));
                keys[i].y = static_cast<int64_t>(std::llround(p.y / step));
                keys[i].z = static_cast<int64_t>(std::llround(p.z / step));
                keys[i].source = static_cast<uint32_t>(i);
            }
            std::sort(keys.begin(), keys.end(), [](const WeldKey& lhs, const WeldKey& rhs) {
                if (lhs.x != rhs.x) { return lhs.x < rhs.x; }
                if (lhs.y != rhs.y) { return lhs.y < rhs.y; }
                if (lhs.z != rhs.z) { return lhs.z < rhs.z; }
                return lhs.source < rhs.source;
            });

            std::vector<uint32_t> remap(count, 0);
            outPositions.clear();
            outPositions.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                const bool same = (i > 0) && keys[i].x == keys[i - 1].x
                    && keys[i].y == keys[i - 1].y && keys[i].z == keys[i - 1].z;
                if (!same) {
                    outPositions.push_back(positions[keys[i].source]);
                }
                remap[keys[i].source] = static_cast<uint32_t>(outPositions.size() - 1);
            }
            return remap;
        }
    }

    std::shared_ptr<const TriangleMesh> TriangleMesh::Build(const TriangleMeshInput& input)
    {
        const auto startTime = std::chrono::steady_clock::now();

        const size_t sourceVertexCount = input.positions.size();
        const size_t sourceTriangleCount = input.indices.size() / 3;
        if (sourceVertexCount == 0 || sourceTriangleCount == 0) {
            return nullptr;
        }

        auto mesh = std::make_shared<TriangleMesh>();
        mesh->stats_.sourceTriangles = static_cast<uint32_t>(sourceTriangleCount);

        // ── 同じ位置の頂点を 1 つにまとめる ───────────────────
        const AABB sourceBounds = AABB::FromPoints(input.positions.data(), sourceVertexCount);
        const Vector3 extent = sourceBounds.GetSize();
        const float diagonal = std::sqrt(Dot(extent, extent));
        const float weldStep = std::clamp(diagonal * 1e-5f, 1e-6f, 1e-3f);
        const float degenerateArea = (std::max)(diagonal * diagonal * kDegenerateAreaRatio, 1e-14f);

        std::vector<Vector3> welded;
        const std::vector<uint32_t> remap = WeldPositions(input.positions, weldStep, welded);

        // ── 三角形を集め、表の向きをそろえる ─────────────────
        const bool hasNormals = input.normals.size() == sourceVertexCount;
        std::vector<uint32_t> indices;
        indices.reserve(sourceTriangleCount * 3);
        for (size_t t = 0; t < sourceTriangleCount; ++t) {
            const uint32_t s0 = input.indices[t * 3 + 0];
            const uint32_t s1 = input.indices[t * 3 + 1];
            const uint32_t s2 = input.indices[t * 3 + 2];
            if (s0 >= sourceVertexCount || s1 >= sourceVertexCount || s2 >= sourceVertexCount) {
                continue;
            }
            uint32_t i0 = remap[s0];
            uint32_t i1 = remap[s1];
            uint32_t i2 = remap[s2];
            if (i0 == i1 || i1 == i2 || i2 == i0) {
                continue;
            }
            const Vector3& a = welded[i0];
            const Vector3& b = welded[i1];
            const Vector3& c = welded[i2];
            const Vector3 cross = Cross(b - a, c - a);
            if (Dot(cross, cross) <= degenerateArea * degenerateArea) {
                continue;
            }
            // 頂点の法線の側を表にする
            if (hasNormals) {
                const Vector3 vertexNormal = input.normals[s0] + input.normals[s1] + input.normals[s2];
                if (Dot(cross, vertexNormal) < 0.0f) {
                    std::swap(i1, i2);
                }
            }
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);
        }

        const uint32_t triangleCount = static_cast<uint32_t>(indices.size() / 3);
        mesh->stats_.removedTriangles = static_cast<uint32_t>(sourceTriangleCount) - triangleCount;
        if (triangleCount == 0) {
            return nullptr;
        }

        // ── 三角形が使う頂点だけを詰めて残す ─────────────────
        {
            std::vector<uint32_t> compact(welded.size(), kNoNeighbor);
            mesh->positions_.reserve(welded.size());
            for (uint32_t& index : indices) {
                if (compact[index] == kNoNeighbor) {
                    compact[index] = static_cast<uint32_t>(mesh->positions_.size());
                    mesh->positions_.push_back(welded[index]);
                }
                index = compact[index];
            }
            mesh->positions_.shrink_to_fit();
        }

        // ── 木を作る ──────────────────────────────────────────
        std::vector<Vector3> centroids(triangleCount);
        std::vector<AABB> triangleBounds(triangleCount);
        for (uint32_t t = 0; t < triangleCount; ++t) {
            const Vector3& a = mesh->positions_[indices[t * 3 + 0]];
            const Vector3& b = mesh->positions_[indices[t * 3 + 1]];
            const Vector3& c = mesh->positions_[indices[t * 3 + 2]];
            centroids[t] = (a + b + c) * (1.0f / 3.0f);
            triangleBounds[t] = AABB(Min3(Min3(a, b), c), Max3(Max3(a, b), c));
        }

        std::vector<uint32_t> order(triangleCount);
        for (uint32_t t = 0; t < triangleCount; ++t) {
            order[t] = t;
        }

        struct BuildItem {
            uint32_t node = 0;
            uint32_t begin = 0;
            uint32_t end = 0;
        };
        std::vector<BuildItem> work;
        mesh->nodes_.reserve(static_cast<size_t>(triangleCount / kLeafSize + 1) * 2);
        mesh->nodes_.push_back(Node{});
        work.push_back({ 0, 0, triangleCount });

        while (!work.empty()) {
            const BuildItem item = work.back();
            work.pop_back();

            AABB bounds;
            AABB centroidBounds;
            for (uint32_t i = item.begin; i < item.end; ++i) {
                const AABB& box = triangleBounds[order[i]];
                bounds = AABB(Min3(bounds.min, box.min), Max3(bounds.max, box.max));
                centroidBounds = centroidBounds.Including(centroids[order[i]]);
            }

            const uint32_t count = item.end - item.begin;
            const Vector3 spread = centroidBounds.GetSize();
            const float largest = (std::max)({ spread.x, spread.y, spread.z });

            if (count <= kLeafSize || !(largest > 0.0f)) {
                Node& leaf = mesh->nodes_[item.node];
                leaf.min = bounds.min;
                leaf.max = bounds.max;
                leaf.start = item.begin;
                leaf.count = count;
                continue;
            }

            // 重心の広がりがいちばん大きい軸で、真ん中の重心で 2 つに割る
            const int axis = (spread.x >= spread.y && spread.x >= spread.z) ? 0 : ((spread.y >= spread.z) ? 1 : 2);
            const uint32_t middle = item.begin + count / 2;
            std::nth_element(order.begin() + item.begin, order.begin() + middle, order.begin() + item.end,
                [&](uint32_t lhs, uint32_t rhs) {
                    return Component(centroids[lhs], axis) < Component(centroids[rhs], axis);
                });

            const uint32_t left = static_cast<uint32_t>(mesh->nodes_.size());
            mesh->nodes_.push_back(Node{});
            mesh->nodes_.push_back(Node{});

            Node& inner = mesh->nodes_[item.node];
            inner.min = bounds.min;
            inner.max = bounds.max;
            inner.start = left;
            inner.count = 0;

            work.push_back({ left + 1, middle, item.end });
            work.push_back({ left, item.begin, middle });
        }

        // 葉の順に三角形を並べ替える
        mesh->indices_.resize(indices.size());
        for (uint32_t i = 0; i < triangleCount; ++i) {
            const uint32_t source = order[i];
            mesh->indices_[i * 3 + 0] = indices[source * 3 + 0];
            mesh->indices_[i * 3 + 1] = indices[source * 3 + 1];
            mesh->indices_[i * 3 + 2] = indices[source * 3 + 2];
        }

        // ── 隣の三角形と縁の角度 ──────────────────────────────
        std::vector<EdgeEntry> edges(static_cast<size_t>(triangleCount) * 3);
        for (uint32_t t = 0; t < triangleCount; ++t) {
            for (uint32_t e = 0; e < 3; ++e) {
                const uint32_t a = mesh->indices_[t * 3 + e];
                const uint32_t b = mesh->indices_[t * 3 + (e + 1) % 3];
                edges[t * 3 + e] = { MakeEdgeKey(a, b), t, e };
            }
        }
        std::sort(edges.begin(), edges.end(), [](const EdgeEntry& lhs, const EdgeEntry& rhs) {
            return lhs.key < rhs.key;
        });

        constexpr float kPi = std::numbers::pi_v<float>;
        const size_t vertexCount = mesh->positions_.size();
        mesh->neighbors_.assign(static_cast<size_t>(triangleCount) * 3, kNoNeighbor);
        mesh->vertexMaxAngles_.assign(vertexCount, -kPi);
        mesh->vertexMinAngles_.assign(vertexCount, kPi);

        const auto faceNormal = [&](uint32_t t) {
            const Vector3& a = mesh->positions_[mesh->indices_[t * 3 + 0]];
            const Vector3& b = mesh->positions_[mesh->indices_[t * 3 + 1]];
            const Vector3& c = mesh->positions_[mesh->indices_[t * 3 + 2]];
            return Normalize(Cross(b - a, c - a));
        };
        const auto recordAngle = [&](uint64_t key, float maxAngle, float minAngle) {
            const uint32_t ends[2] = { static_cast<uint32_t>(key >> 32), static_cast<uint32_t>(key & 0xFFFFFFFFu) };
            for (const uint32_t vertex : ends) {
                mesh->vertexMaxAngles_[vertex] = (std::max)(mesh->vertexMaxAngles_[vertex], maxAngle);
                mesh->vertexMinAngles_[vertex] = (std::min)(mesh->vertexMinAngles_[vertex], minAngle);
            }
        };

        for (size_t begin = 0; begin < edges.size();) {
            size_t end = begin + 1;
            while (end < edges.size() && edges[end].key == edges[begin].key) {
                ++end;
            }

            const EdgeEntry& first = edges[begin];
            const EdgeEntry& second = edges[(end - begin >= 2) ? begin + 1 : begin];
            // 表裏がそろった 2 枚は、共有する縁を逆の向きにたどる
            const bool paired = (end - begin == 2)
                && mesh->indices_[first.triangle * 3 + first.edge] != mesh->indices_[second.triangle * 3 + second.edge];

            if (!paired) {
                // 外周の縁・3 枚以上が共有する縁・表裏が食い違う縁は、どちらから見ても出っ張りとする
                recordAngle(first.key, kPi, -kPi);
                begin = end;
                continue;
            }

            mesh->neighbors_[first.triangle * 3 + first.edge] = second.triangle;
            mesh->neighbors_[second.triangle * 3 + second.edge] = first.triangle;

            // 2 枚のなす角。隣の三角形の向こうの頂点が面の表側ならへこみ、裏側なら出っ張り
            const Vector3 normal = faceNormal(first.triangle);
            const Vector3 otherNormal = faceNormal(second.triangle);
            const float angle = std::acos(std::clamp(Dot(normal, otherNormal), -1.0f, 1.0f));
            const uint32_t opposite = mesh->indices_[second.triangle * 3 + (second.edge + 2) % 3];
            const Vector3& edgePoint = mesh->positions_[mesh->indices_[first.triangle * 3 + first.edge]];
            const float side = Dot(normal, mesh->positions_[opposite] - edgePoint);
            const float signedAngle = (side > 0.0f) ? -angle : angle;
            recordAngle(first.key, signedAngle, signedAngle);

            begin = end;
        }

        mesh->bounds_ = AABB(mesh->nodes_[0].min, mesh->nodes_[0].max);

        mesh->stats_.triangles = triangleCount;
        mesh->stats_.vertices = static_cast<uint32_t>(vertexCount);
        mesh->stats_.nodes = static_cast<uint32_t>(mesh->nodes_.size());
        mesh->stats_.memoryBytes = mesh->positions_.size() * sizeof(Vector3)
            + mesh->indices_.size() * sizeof(uint32_t)
            + mesh->neighbors_.size() * sizeof(uint32_t)
            + (mesh->vertexMaxAngles_.size() + mesh->vertexMinAngles_.size()) * sizeof(float)
            + mesh->nodes_.size() * sizeof(Node);

        const auto endTime = std::chrono::steady_clock::now();
        mesh->stats_.buildMilliseconds =
            std::chrono::duration<float, std::milli>(endTime - startTime).count();
        return mesh;
    }

    bool TriangleMesh::TriangleOverlaps(uint32_t triangle, const AABB& box) const
    {
        Vector3 a, b, c;
        Corners(triangle, a, b, c);
        const Vector3 low = Min3(Min3(a, b), c);
        const Vector3 high = Max3(Max3(a, b), c);
        return low.x <= box.max.x && high.x >= box.min.x
            && low.y <= box.max.y && high.y >= box.min.y
            && low.z <= box.max.z && high.z >= box.min.z;
    }

    bool TriangleMesh::Raycast(const Ray& ray, float tMin, float tMax, bool frontOnly,
                               RayHit* outHit, uint32_t* outTriangle) const
    {
        if (nodes_.empty()) {
            return false;
        }

        // 軸ごとの向きの逆数（0 の軸は大きな値にして、その軸の板を無限に広いとみなす）
        const auto inverse = [](float value) {
            return (std::abs(value) > 1e-30f) ? 1.0f / value : ((value >= 0.0f) ? 1e30f : -1e30f);
        };
        const Vector3 inverseDirection{ inverse(ray.direction.x), inverse(ray.direction.y), inverse(ray.direction.z) };

        const auto nodeEntry = [&](const Node& node, float limit, float& outEntry) {
            const float tx1 = (node.min.x - ray.origin.x) * inverseDirection.x;
            const float tx2 = (node.max.x - ray.origin.x) * inverseDirection.x;
            const float ty1 = (node.min.y - ray.origin.y) * inverseDirection.y;
            const float ty2 = (node.max.y - ray.origin.y) * inverseDirection.y;
            const float tz1 = (node.min.z - ray.origin.z) * inverseDirection.z;
            const float tz2 = (node.max.z - ray.origin.z) * inverseDirection.z;
            const float entry = (std::max)({ (std::min)(tx1, tx2), (std::min)(ty1, ty2), (std::min)(tz1, tz2), tMin });
            const float exit = (std::min)({ (std::max)(tx1, tx2), (std::max)(ty1, ty2), (std::max)(tz1, tz2), limit });
            outEntry = entry;
            return entry <= exit;
        };

        bool found = false;
        float nearest = tMax;
        uint32_t nearestTriangle = 0;
        RayHit nearestHit{};

        uint32_t stack[64];
        int top = 0;
        stack[top++] = 0;
        while (top > 0) {
            const Node& node = nodes_[stack[--top]];
            float entry = 0.0f;
            if (!nodeEntry(node, nearest, entry)) {
                continue;
            }
            if (node.count > 0) {
                for (uint32_t triangle = node.start; triangle < node.start + node.count; ++triangle) {
                    Vector3 a, b, c;
                    Corners(triangle, a, b, c);
                    RayHit hit{};
                    if (!RaycastTriangle(ray, a, b, c, &hit, tMin, nearest)) {
                        continue;
                    }
                    const Vector3 front = Normalize(Cross(b - a, c - a));
                    if (frontOnly && Dot(front, ray.direction) >= 0.0f) {
                        continue;
                    }
                    found = true;
                    nearest = hit.distance;
                    nearestTriangle = triangle;
                    nearestHit = hit;
                    nearestHit.normal = front;
                }
                continue;
            }

            // 近い子を後に積んで先に調べる
            float leftEntry = 0.0f;
            float rightEntry = 0.0f;
            const uint32_t left = node.start;
            const uint32_t right = node.start + 1;
            const bool hitLeft = nodeEntry(nodes_[left], nearest, leftEntry);
            const bool hitRight = nodeEntry(nodes_[right], nearest, rightEntry);
            if (top + 2 > 64) {
                continue;
            }
            if (hitLeft && hitRight) {
                if (leftEntry <= rightEntry) {
                    stack[top++] = right;
                    stack[top++] = left;
                } else {
                    stack[top++] = left;
                    stack[top++] = right;
                }
            } else if (hitLeft) {
                stack[top++] = left;
            } else if (hitRight) {
                stack[top++] = right;
            }
        }

        if (found) {
            if (outHit) { *outHit = nearestHit; }
            if (outTriangle) { *outTriangle = nearestTriangle; }
        }
        return found;
    }
}
}
