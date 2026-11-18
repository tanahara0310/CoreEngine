#pragma once

#include "Math/Geometry/RayCast.h"
#include "Math/Geometry/Shapes.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

/// @file
/// @brief 三角形の集まりと、それを外接箱の木（BVH）にまとめたもの
/// @details 当たり判定のメッシュの中身。頂点はモデルのローカルのまま持ち、
///          ワールドへの移し方は使う側（MeshView）が決める。

namespace CoreEngine
{
namespace Geometry
{
    /// @brief 三角形の集まりを作るための材料
    struct TriangleMeshInput {
        std::vector<Vector3>  positions;  ///< 頂点の位置
        std::vector<Vector3>  normals;    ///< 頂点の法線（表の向きを決める。空なら並び順のまま）
        std::vector<uint32_t> indices;    ///< 三角形ごとに頂点の番号 3 つ
    };

    /// @brief 作った結果の数と大きさ
    struct TriangleMeshStats {
        uint32_t sourceTriangles = 0;   ///< 材料の三角形の数
        uint32_t triangles = 0;         ///< 使う三角形の数
        uint32_t removedTriangles = 0;  ///< つぶれていて外した三角形の数
        uint32_t vertices = 0;          ///< 同じ位置をまとめた後の頂点の数
        uint32_t nodes = 0;             ///< 木の節の数
        size_t   memoryBytes = 0;       ///< 持っている配列の大きさ
        float    buildMilliseconds = 0.0f;
    };

    /// @brief 三角形の集まり（作った後は変えない）
    class TriangleMesh {
    public:
        /// @brief 木の節（32 バイト）
        /// @details count が 0 なら内側の節で、子は start と start + 1。0 でなければ葉で、三角形 [start, start + count)。
        struct Node {
            Vector3  min;
            uint32_t start = 0;
            Vector3  max;
            uint32_t count = 0;
        };

        /// @brief 隣の三角形が無い（外周の縁・3 枚以上が共有する縁・表裏の向きが食い違う縁）
        static constexpr uint32_t kNoNeighbor = 0xFFFFFFFFu;

        /// @brief 材料から作る
        /// @details 同じ位置の頂点をまとめ、つぶれた三角形を外し、隣の三角形と木を作る。
        ///          使える三角形が 1 つも無ければ nullptr。
        static std::shared_ptr<const TriangleMesh> Build(const TriangleMeshInput& input);

        uint32_t TriangleCount() const { return static_cast<uint32_t>(indices_.size() / 3); }
        uint32_t VertexCount() const { return static_cast<uint32_t>(positions_.size()); }

        /// @brief 三角形の頂点の番号（表から見て、外積 (b-a)×(c-a) が表を向く並び）
        uint32_t VertexIndex(uint32_t triangle, int corner) const { return indices_[triangle * 3 + corner]; }

        /// @brief 三角形の 3 頂点（ローカル）
        void Corners(uint32_t triangle, Vector3& a, Vector3& b, Vector3& c) const
        {
            const uint32_t* const index = &indices_[triangle * 3];
            a = positions_[index[0]];
            b = positions_[index[1]];
            c = positions_[index[2]];
        }

        const Vector3& Position(uint32_t vertex) const { return positions_[vertex]; }

        /// @brief 辺 edge（0 = a-b・1 = b-c・2 = c-a）を共有する隣の三角形（無ければ kNoNeighbor）
        uint32_t Neighbor(uint32_t triangle, int edge) const { return neighbors_[triangle * 3 + edge]; }

        /// @brief 頂点につながる縁のうち、いちばん出っ張った縁の角度（ラジアン）
        /// @param fromBack true なら裏から見たときの出っ張り
        /// @details 出っ張りは正・へこみは負・平らは 0。隣の無い縁は π。
        float VertexAngle(uint32_t vertex, bool fromBack = false) const
        {
            return fromBack ? -vertexMinAngles_[vertex] : vertexMaxAngles_[vertex];
        }

        /// @brief 全体のローカルの外接箱
        const AABB& Bounds() const { return bounds_; }

        const TriangleMeshStats& Stats() const { return stats_; }

        /// @brief ローカルの箱と外接箱が重なる三角形を順に渡す
        /// @param fn void(uint32_t triangle)
        template <class Fn>
        void ForEachTriangleInBox(const AABB& box, Fn&& fn) const;

        /// @brief ローカルのレイで最も近い三角形を探す
        /// @param ray 向きは正規化しなくてよい（t は向きの長さを単位にする）
        /// @param frontOnly true なら表から当たる三角形だけ
        /// @param outHit 法線は三角形の表の向き（ローカル・正規化）
        bool Raycast(const Ray& ray, float tMin, float tMax, bool frontOnly,
                     RayHit* outHit, uint32_t* outTriangle) const;

    private:
        static bool Overlaps(const Node& node, const AABB& box)
        {
            return node.min.x <= box.max.x && node.max.x >= box.min.x
                && node.min.y <= box.max.y && node.max.y >= box.min.y
                && node.min.z <= box.max.z && node.max.z >= box.min.z;
        }

        bool TriangleOverlaps(uint32_t triangle, const AABB& box) const;

        std::vector<Vector3>  positions_;
        std::vector<uint32_t> indices_;       ///< 3 つずつ。木の葉の順に並べ替えてある
        std::vector<uint32_t> neighbors_;     ///< 三角形ごとに 3 つ（辺 0・1・2）
        std::vector<float>    vertexMaxAngles_;   ///< 表から見ていちばん出っ張った縁の角度
        std::vector<float>    vertexMinAngles_;   ///< 表から見ていちばんへこんだ縁の角度
        std::vector<Node>     nodes_;
        AABB                  bounds_;
        TriangleMeshStats     stats_;
    };

    template <class Fn>
    void TriangleMesh::ForEachTriangleInBox(const AABB& box, Fn&& fn) const
    {
        if (nodes_.empty()) {
            return;
        }
        uint32_t stack[64];
        int top = 0;
        stack[top++] = 0;
        while (top > 0) {
            const Node& node = nodes_[stack[--top]];
            if (!Overlaps(node, box)) {
                continue;
            }
            if (node.count > 0) {
                for (uint32_t triangle = node.start; triangle < node.start + node.count; ++triangle) {
                    if (TriangleOverlaps(triangle, box)) {
                        fn(triangle);
                    }
                }
                continue;
            }
            if (top + 2 <= 64) {
                stack[top++] = node.start + 1;
                stack[top++] = node.start;
            }
        }
    }
}
}
