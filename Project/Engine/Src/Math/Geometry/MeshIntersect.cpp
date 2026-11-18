#include "pch.h"
#include "Math/Geometry/MeshView.h"
#include "Math/Geometry/Triangle.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace CoreEngine
{
namespace Geometry
{
    namespace {
        /// @brief 1 回の問い合わせで控える接触の数の上限（超えたら浅いものから捨てる）
        constexpr int kMaxCandidates = 128;

        /// @brief 同じ向きとみなす法線の内積（1 度）
        constexpr float kMergeCosine = 0.99985f;

        /// @brief 向きが決まらないほど近い距離
        constexpr float kTiny = 1e-6f;

        /// @brief 剛体を支える点を選ぶとき、別の面とみなす法線の内積（約 18 度）
        constexpr float kOtherFaceCosine = 0.95f;

        /// @brief 三角形を箱で切り取った多角形の頂点の数の上限（3 頂点＋6 面で 1 つずつ）
        constexpr int kMaxClipVertices = 9;

        enum class ContactKind : uint8_t {
            Face,    ///< 三角形の面の向きで押す
            Radial,  ///< 出っ張りの縁・頂点から形の芯へ向かう向きで押す
        };

        /// @brief 三角形 1 つとの接触
        struct RawContact {
            Vector3     push{ 0.0f, 0.0f, 0.0f };   ///< メッシュから形へ向かう押し出しの向き
            float       depth = 0.0f;
            Vector3     point{ 0.0f, 0.0f, 0.0f };
            ContactKind kind = ContactKind::Face;
            uint32_t    vertices[3] = {};           ///< 触れている部分の頂点（面なら 3・縁なら 2・頂点なら 1）
            int         vertexCount = 0;
        };

        /// @brief 接触の控え（数が上限を超えたら浅いものを捨てる）
        class CandidateList {
        public:
            void Add(const RawContact& contact)
            {
                if (count_ < kMaxCandidates) {
                    items_[count_++] = contact;
                    return;
                }
                int shallowest = 0;
                for (int i = 1; i < count_; ++i) {
                    if (items_[i].depth < items_[shallowest].depth) {
                        shallowest = i;
                    }
                }
                if (contact.depth > items_[shallowest].depth) {
                    items_[shallowest] = contact;
                }
            }

            RawContact* begin() { return items_; }
            RawContact* end() { return items_ + count_; }

        private:
            RawContact items_[kMaxCandidates];
            int        count_ = 0;
        };

        /// @brief ワールドへ移した三角形
        struct WorldTriangle {
            uint32_t index = 0;
            Vector3  corners[3];
            Vector3  front{ 0.0f, 0.0f, 0.0f };  ///< 表の向き（正規化）
            bool     valid = false;
        };

        WorldTriangle MakeWorldTriangle(const MeshView& view, uint32_t triangle)
        {
            WorldTriangle result;
            result.index = triangle;
            view.WorldTriangle(triangle, result.corners[0], result.corners[1], result.corners[2]);
            const Vector3 cross = Cross(result.corners[1] - result.corners[0], result.corners[2] - result.corners[0]);
            const float lengthSq = Dot(cross, cross);
            if (!(lengthSq > 1e-24f)) {
                return result;
            }
            // 鏡写しでは並び順が逆になるので、外積の向きを戻して表にする
            const float scale = (view.Mirrored() ? -1.0f : 1.0f) / std::sqrt(lengthSq);
            result.front = cross * scale;
            result.valid = true;
            return result;
        }

        /// @brief 縁の性質
        struct EdgeInfo {
            bool    internal = false;      ///< 芯から見て平らかへこみ
            bool    hasNeighbor = false;
            Vector3 intoNeighbor{ 0.0f, 0.0f, 0.0f };  ///< 縁から隣の三角形の内側へ向かう向き（縁に垂直）
        };

        /// @brief 三角形の辺 edge を、隣の三角形との角度と芯の位置で調べる
        /// @param core 形の芯の点
        /// @param side 芯がこの三角形のどちら側にいるか（表 +1・裏 -1）
        /// @details へこみの縁は、芯が隣の三角形からも同じ側にいるとき（谷の内側にいるとき）だけ内側とする。
        EdgeInfo ClassifyEdge(const MeshView& view, const WorldTriangle& triangle, int edge,
                              const Vector3& core, float side, float flatCosine)
        {
            EdgeInfo info;
            const uint32_t neighbor = view.mesh->Neighbor(triangle.index, edge);
            if (neighbor == TriangleMesh::kNoNeighbor) {
                return info;
            }
            const WorldTriangle other = MakeWorldTriangle(view, neighbor);
            if (!other.valid) {
                return info;
            }

            // 隣の三角形の、縁に乗っていない頂点
            const uint32_t edgeStart = view.mesh->VertexIndex(triangle.index, edge);
            const uint32_t edgeEnd = view.mesh->VertexIndex(triangle.index, (edge + 1) % 3);
            Vector3 opposite = other.corners[0];
            for (int corner = 0; corner < 3; ++corner) {
                const uint32_t vertex = view.mesh->VertexIndex(neighbor, corner);
                if (vertex != edgeStart && vertex != edgeEnd) {
                    opposite = other.corners[corner];
                    break;
                }
            }

            const Vector3& start = triangle.corners[edge];
            const Vector3 along = Normalize(triangle.corners[(edge + 1) % 3] - start);
            const Vector3 toOpposite = opposite - start;
            info.intoNeighbor = Normalize(toOpposite - along * Dot(toOpposite, along));
            info.hasNeighbor = true;

            if (Dot(triangle.front, other.front) >= flatCosine) {
                info.internal = true;
                return info;
            }
            const bool concave = Dot(triangle.front * side, toOpposite) > 0.0f;
            info.internal = concave && side * Dot(core - start, other.front) > 0.0f;
            return info;
        }

        /// @brief 頂点 corner が、形のいる側から見て平らかへこみの縁だけに囲まれているか
        bool IsInternalVertex(const MeshView& view, const WorldTriangle& triangle, int corner, float side)
        {
            const uint32_t vertex = view.mesh->VertexIndex(triangle.index, corner);
            return view.mesh->VertexAngle(vertex, side < 0.0f) <= view.flatEdgeAngle;
        }

        int EdgeOf(TriangleFeature feature)
        {
            return static_cast<int>(feature) - static_cast<int>(TriangleFeature::Edge0);
        }

        int CornerOf(TriangleFeature feature)
        {
            return static_cast<int>(feature) - static_cast<int>(TriangleFeature::Vertex0);
        }

        void SetFaceVertices(const MeshView& view, uint32_t triangle, RawContact& contact)
        {
            for (int corner = 0; corner < 3; ++corner) {
                contact.vertices[corner] = view.mesh->VertexIndex(triangle, corner);
            }
            contact.vertexCount = 3;
        }

        /// @brief 芯の点 core と、それにいちばん近い三角形の上の点から接触を作る（球とカプセル）
        /// @param side 形がいる側（表 +1・裏 -1）
        /// @param sideDistance core の、side の側へ測った面からの距離
        void AddRoundContact(const MeshView& view, const WorldTriangle& triangle, const Vector3& core,
                             const TrianglePoint& onTriangle, float radius, float side, float sideDistance,
                             float flatCosine, CandidateList& out)
        {
            const Vector3 delta = core - onTriangle.point;
            const float distanceSq = Dot(delta, delta);
            if (distanceSq > radius * radius) {
                return;
            }
            const float distance = std::sqrt(distanceSq);
            // 芯が縁や頂点の向こうへ出ていると見なす横のずれ
            const float lateralTolerance = radius * 1e-4f;

            RawContact contact;
            contact.point = onTriangle.point;

            bool useFace = false;
            const TriangleFeature feature = onTriangle.feature;
            if (feature == TriangleFeature::Face) {
                useFace = true;
            }
            else if (feature == TriangleFeature::Edge0 || feature == TriangleFeature::Edge1
                  || feature == TriangleFeature::Edge2) {
                const int edge = EdgeOf(feature);
                const EdgeInfo info = ClassifyEdge(view, triangle, edge, core, side, flatCosine);
                // 芯が縁の向こう（隣の三角形の側）へ出ているなら、隣の三角形の接触に任せる
                const bool beyond = info.hasNeighbor && Dot(delta, info.intoNeighbor) > lateralTolerance;
                if (info.internal) {
                    if (beyond) {
                        return;
                    }
                    useFace = true;
                }
                else {
                    if (beyond) {
                        return;
                    }
                    contact.vertices[0] = view.mesh->VertexIndex(triangle.index, edge);
                    contact.vertices[1] = view.mesh->VertexIndex(triangle.index, (edge + 1) % 3);
                    contact.vertexCount = 2;
                }
            }
            else {
                const int corner = CornerOf(feature);
                if (IsInternalVertex(view, triangle, corner, side)) {
                    // 芯が頂点の向こうへ出ているなら、頂点を囲むほかの三角形の接触に任せる
                    const Vector3 lateral = delta - triangle.front * Dot(delta, triangle.front);
                    if (Dot(lateral, lateral) > lateralTolerance * lateralTolerance) {
                        return;
                    }
                    useFace = true;
                }
                else {
                    contact.vertices[0] = view.mesh->VertexIndex(triangle.index, corner);
                    contact.vertexCount = 1;
                }
            }

            if (useFace) {
                contact.kind = ContactKind::Face;
                contact.push = triangle.front * side;
                contact.depth = radius - sideDistance;
                SetFaceVertices(view, triangle.index, contact);
            }
            else {
                contact.kind = ContactKind::Radial;
                contact.push = (distance > kTiny) ? delta * (1.0f / distance) : triangle.front * side;
                contact.depth = radius - distance;
            }
            out.Add(contact);
        }

        /// @brief 押す向き push の先にある三角形の部分が、内側の縁・頂点でないか
        /// @param core 形の芯の点
        /// @param outContact 触れている部分の頂点を書き込む
        bool SupportAllowsPush(const MeshView& view, const WorldTriangle& triangle, const Vector3& push,
                               const Vector3& core, float side, float flatCosine, RawContact& outContact)
        {
            const float d[3] = {
                Dot(triangle.corners[0], push), Dot(triangle.corners[1], push), Dot(triangle.corners[2], push)
            };
            const float highest = (std::max)({ d[0], d[1], d[2] });
            const float lowest = (std::min)({ d[0], d[1], d[2] });
            const float tolerance = (highest - lowest) * 1e-3f + kTiny;

            int corners[3] = {};
            int count = 0;
            for (int corner = 0; corner < 3; ++corner) {
                if (d[corner] >= highest - tolerance) {
                    corners[count++] = corner;
                }
            }

            if (count == 3) {
                outContact.kind = ContactKind::Face;
                SetFaceVertices(view, triangle.index, outContact);
                return true;
            }

            outContact.kind = ContactKind::Radial;
            if (count == 2) {
                // 辺 0 = 頂点 0-1・辺 1 = 頂点 1-2・辺 2 = 頂点 2-0
                const int edge = (corners[0] == 0 && corners[1] == 2) ? 2 : corners[0];
                if (ClassifyEdge(view, triangle, edge, core, side, flatCosine).internal) {
                    return false;
                }
                outContact.vertices[0] = view.mesh->VertexIndex(triangle.index, corners[0]);
                outContact.vertices[1] = view.mesh->VertexIndex(triangle.index, corners[1]);
                outContact.vertexCount = 2;
                return true;
            }

            if (IsInternalVertex(view, triangle, corners[0], side)) {
                return false;
            }
            outContact.vertices[0] = view.mesh->VertexIndex(triangle.index, corners[0]);
            outContact.vertexCount = 1;
            return true;
        }

        float AxisOf(const Vector3& v, int axis)
        {
            return (axis == 0) ? v.x : ((axis == 1) ? v.y : v.z);
        }

        /// @brief 箱のローカルへ移した三角形を、箱の 6 面で切り取る
        /// @param tolerance 箱の面の外側に許す幅
        /// @return 残った多角形の頂点の数（0 なら箱と重ならない）
        int ClipTriangleToBox(const Vector3 local[3], const Vector3& halfExtents, float tolerance,
                              Vector3 out[kMaxClipVertices])
        {
            Vector3 bufferA[kMaxClipVertices];
            Vector3 bufferB[kMaxClipVertices];
            Vector3* input = bufferA;
            Vector3* output = bufferB;
            int count = 3;
            for (int i = 0; i < 3; ++i) {
                input[i] = local[i];
            }

            for (int plane = 0; plane < 6; ++plane) {
                const int axis = plane / 2;
                const float sign = (plane % 2 == 0) ? 1.0f : -1.0f;
                const float limit = AxisOf(halfExtents, axis) + tolerance;

                // sign * p[axis] <= limit の側を残す
                int written = 0;
                for (int i = 0; i < count; ++i) {
                    const Vector3& from = input[i];
                    const Vector3& to = input[(i + 1) % count];
                    const float fromOver = sign * AxisOf(from, axis) - limit;
                    const float toOver = sign * AxisOf(to, axis) - limit;
                    if (fromOver <= 0.0f) {
                        output[written++] = from;
                    }
                    if ((fromOver <= 0.0f) != (toOver <= 0.0f) && written < kMaxClipVertices) {
                        const float t = fromOver / (fromOver - toOver);
                        output[written++] = from + (to - from) * t;
                    }
                }
                count = written;
                std::swap(input, output);
                if (count == 0) {
                    return 0;
                }
            }
            for (int i = 0; i < count; ++i) {
                out[i] = input[i];
            }
            return count;
        }

        /// @brief 箱の中の点 local から、向き direction（箱のローカル）へ進んで箱を出るまでの距離
        float ExitDistance(const Vector3& local, const Vector3& direction, const Vector3& halfExtents)
        {
            float result = FLT_MAX;
            for (int axis = 0; axis < 3; ++axis) {
                const float d = AxisOf(direction, axis);
                if (std::abs(d) <= kTiny) {
                    continue;
                }
                const float p = AxisOf(local, axis);
                const float e = AxisOf(halfExtents, axis);
                const float room = (d > 0.0f) ? (e - p) : (e + p);
                result = (std::min)(result, room / std::abs(d));
            }
            return (result == FLT_MAX) ? 0.0f : (std::max)(result, 0.0f);
        }

        /// @brief 押す向きの決まった箱と三角形の接触 base から、箱を支える点を集める
        /// @details 面で押す接触は、面の下に入った箱の頂点のうち、面の真下が三角形の中にあるものを点にする
        ///          （三角形の外に出た頂点は隣の三角形が拾う）。
        ///          加えて、三角形のうち箱の中に入った部分の頂点を点にする。深さは押す向きの逆へ箱を出るまでの距離。
        ///          点の位置は、三角形の表と箱の奥の中ほど。
        /// @return 足した点の数
        int AddBoxPoints(const WorldTriangle& triangle, const OBB& box, const RawContact& base, CandidateList& out)
        {
            const Vector3& push = base.push;
            const Vector3* const c = triangle.corners;
            const float largest = (std::max)({ box.halfExtents.x, box.halfExtents.y, box.halfExtents.z });
            const float tolerance = largest * 1e-3f + kTiny;
            int added = 0;

            const auto add = [&](const Vector3& point, float depth) {
                RawContact contact = base;
                contact.point = point;
                contact.depth = (std::min)(depth, base.depth);
                out.Add(contact);
                ++added;
            };

            if (base.kind == ContactKind::Face) {
                Vector3 corners[8];
                box.Corners(corners);
                const float surface = Dot(c[0], push);
                for (const Vector3& corner : corners) {
                    const float depth = surface - Dot(corner, push);
                    if (!(depth > 0.0f)) {
                        continue;
                    }
                    const Vector3 onPlane = corner + push * depth;
                    const Vector3 off = ClosestPointOnTriangle(onPlane, c[0], c[1], c[2]).point - onPlane;
                    if (Dot(off, off) > tolerance * tolerance) {
                        continue;
                    }
                    add(corner + push * (depth * 0.5f), depth);
                }
            }

            const Vector3 local[3] = { box.ToLocal(c[0]), box.ToLocal(c[1]), box.ToLocal(c[2]) };
            Vector3 clipped[kMaxClipVertices];
            const int count = ClipTriangleToBox(local, box.halfExtents, tolerance, clipped);
            const Vector3 back{ -Dot(push, box.axes[0]), -Dot(push, box.axes[1]), -Dot(push, box.axes[2]) };
            for (int i = 0; i < count; ++i) {
                const float depth = ExitDistance(clipped[i], back, box.halfExtents);
                const Vector3 point = box.center + box.axes[0] * clipped[i].x
                                    + box.axes[1] * clipped[i].y + box.axes[2] * clipped[i].z;
                add(point - push * (depth * 0.5f), depth);
            }
            return added;
        }

        /// @brief 箱と三角形を分離軸で調べ、重なりがいちばん小さい向きの接触を作る
        /// @details 押す向きの先にある三角形の部分が内側の縁・頂点になる軸は、押す向きに選ばない。
        ///          Manifold では箱を支える点を全部足し、Deepest では代表の点 1 つを足す。
        void AddBoxContact(const MeshView& view, const WorldTriangle& triangle, const OBB& box,
                           float side, float flatCosine, MeshContactMode mode, CandidateList& out)
        {
            const Vector3* const c = triangle.corners;
            const Vector3 edges[3] = { c[1] - c[0], c[2] - c[1], c[0] - c[2] };

            Vector3 axes[13];
            int axisCount = 0;
            axes[axisCount++] = triangle.front * side;
            for (int i = 0; i < 3; ++i) {
                axes[axisCount++] = box.axes[i];
            }
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    const Vector3 axis = Cross(box.axes[i], edges[j]);
                    const float lengthSq = Dot(axis, axis);
                    if (lengthSq > 1e-10f * Dot(edges[j], edges[j])) {
                        axes[axisCount++] = axis * (1.0f / std::sqrt(lengthSq));
                    }
                }
            }

            bool found = false;
            RawContact best;
            best.depth = FLT_MAX;
            for (int a = 0; a < axisCount; ++a) {
                const Vector3& axis = axes[a];
                const float d0 = Dot(c[0], axis);
                const float d1 = Dot(c[1], axis);
                const float d2 = Dot(c[2], axis);
                const float triangleMin = (std::min)({ d0, d1, d2 });
                const float triangleMax = (std::max)({ d0, d1, d2 });
                const float center = Dot(box.center, axis);
                const float radius = box.ProjectedRadius(axis);
                if (center - radius > triangleMax || center + radius < triangleMin) {
                    return;  // この軸で分かれている
                }

                // 箱を +axis へ押して離す量と、-axis へ押して離す量
                const float pushPositive = triangleMax - (center - radius);
                const float pushNegative = (center + radius) - triangleMin;
                const bool positive = pushPositive <= pushNegative;
                const float depth = positive ? pushPositive : pushNegative;
                if (!(depth < best.depth)) {
                    continue;
                }
                const Vector3 push = positive ? axis : axis * -1.0f;
                RawContact candidate;
                if (!SupportAllowsPush(view, triangle, push, box.center, side, flatCosine, candidate)) {
                    continue;
                }
                candidate.push = push;
                candidate.depth = depth;
                best = candidate;
                found = true;
            }
            if (!found) {
                return;
            }
            if (mode == MeshContactMode::Manifold && AddBoxPoints(triangle, box, best, out) > 0) {
                return;
            }

            // 箱の、押す向きと逆でいちばん奥の点から、深さの半分だけ戻した点を代表にする
            Vector3 deepest = box.center;
            for (int i = 0; i < 3; ++i) {
                const float sign = (Dot(box.axes[i], best.push) > 0.0f) ? -1.0f : 1.0f;
                deepest = deepest + box.axes[i] * (box.Extent(i) * sign);
            }
            best.point = deepest + best.push * (best.depth * 0.5f);
            out.Add(best);
        }

        /// @brief 深い順に並んだ接触から maxContacts 個を選ぶ
        /// @details いちばん深い接触から始め、まだ選んでいない向きの面の接触を深い順に 1 つずつ足す。
        ///          残りは、選んだ点からいちばん遠い点を 1 つずつ足す。
        int PickManifold(const Contact* contacts, int count, Contact* outContacts, int maxContacts)
        {
            bool used[kMaxCandidates] = {};
            int written = 0;
            const auto take = [&](int index) {
                outContacts[written++] = contacts[index];
                used[index] = true;
            };

            take(0);
            for (int i = 1; i < count && written < maxContacts; ++i) {
                bool otherFace = true;
                for (int j = 0; j < written; ++j) {
                    if (Dot(outContacts[j].normal, contacts[i].normal) >= kOtherFaceCosine) {
                        otherFace = false;
                        break;
                    }
                }
                if (otherFace) {
                    take(i);
                }
            }

            while (written < maxContacts) {
                int farthest = -1;
                float farthestDistanceSq = -1.0f;
                for (int i = 0; i < count; ++i) {
                    if (used[i]) {
                        continue;
                    }
                    float nearestSq = FLT_MAX;
                    for (int j = 0; j < written; ++j) {
                        const Vector3 delta = contacts[i].point - outContacts[j].point;
                        nearestSq = (std::min)(nearestSq, Dot(delta, delta));
                    }
                    if (nearestSq > farthestDistanceSq) {
                        farthestDistanceSq = nearestSq;
                        farthest = i;
                    }
                }
                if (farthest < 0) {
                    break;
                }
                take(farthest);
            }
            return written;
        }

        /// @brief 控えた接触を深い順に並べ、縁の重なりとほぼ同じ接触を省いて書き出す
        /// @details 面の向きで押す接触が触れた三角形の頂点だけでできた出っ張りの接触は捨てる
        ///          （面の接触の方が正しい向きを持っている）。
        ///          Deepest は深い順に上限まで、Manifold は上限を超えたら PickManifold で選ぶ。
        int Finish(CandidateList& list, float mergeDistance, MeshContactMode mode,
                   Contact* outContacts, int maxContacts)
        {
            std::sort(list.begin(), list.end(), [](const RawContact& lhs, const RawContact& rhs) {
                return lhs.depth > rhs.depth;
            });

            uint32_t covered[kMaxCandidates * 3];
            int coveredCount = 0;
            for (const RawContact& contact : list) {
                if (contact.kind != ContactKind::Face) {
                    continue;
                }
                for (int i = 0; i < contact.vertexCount; ++i) {
                    covered[coveredCount++] = contact.vertices[i];
                }
            }
            const auto isCovered = [&](uint32_t vertex) {
                for (int i = 0; i < coveredCount; ++i) {
                    if (covered[i] == vertex) {
                        return true;
                    }
                }
                return false;
            };

            const float mergeDistanceSq = mergeDistance * mergeDistance;
            Contact kept[kMaxCandidates];
            const int limit = (mode == MeshContactMode::Manifold) ? kMaxCandidates : maxContacts;
            int written = 0;
            for (const RawContact& contact : list) {
                if (written >= limit) {
                    break;
                }
                if (contact.kind == ContactKind::Radial && contact.vertexCount > 0) {
                    bool allCovered = true;
                    for (int i = 0; i < contact.vertexCount; ++i) {
                        allCovered = allCovered && isCovered(contact.vertices[i]);
                    }
                    if (allCovered) {
                        continue;
                    }
                }

                const Vector3 normal = contact.push * -1.0f;
                bool merged = false;
                for (int i = 0; i < written; ++i) {
                    const Vector3 delta = kept[i].point - contact.point;
                    if (Dot(kept[i].normal, normal) >= kMergeCosine && Dot(delta, delta) <= mergeDistanceSq) {
                        merged = true;
                        break;
                    }
                }
                if (merged) {
                    continue;
                }

                Contact& out = kept[written++];
                out.normal = normal;
                out.depth = (std::max)(contact.depth, 0.0f);
                out.point = contact.point;
            }

            if (written > maxContacts) {
                return PickManifold(kept, written, outContacts, maxContacts);
            }
            for (int i = 0; i < written; ++i) {
                outContacts[i] = kept[i];
            }
            return written;
        }

        AABB BoxAround(const Vector3& low, const Vector3& high, float margin)
        {
            const Vector3 m{ margin, margin, margin };
            return AABB(low - m, high + m);
        }

        /// @brief 形とメッシュの代表の接触（いちばん深いもの）を求め、法線を A→B にそろえる
        template <typename Shape>
        bool IntersectShape(const Shape& shape, const MeshView& mesh, Contact* outContact)
        {
            Contact contact;
            if (CollectMeshContacts(shape, mesh, &contact, 1) == 0) {
                return false;
            }
            if (outContact) {
                *outContact = contact;
            }
            return true;
        }

        template <typename Shape>
        bool IntersectMeshFirst(const MeshView& mesh, const Shape& shape, Contact* outContact)
        {
            const bool hit = IntersectShape(shape, mesh, outContact);
            if (hit && outContact) {
                outContact->normal = outContact->normal * -1.0f;
            }
            return hit;
        }
    }

    //================================================
    // 見え方
    //================================================

    AABB MeshView::LocalBoxOf(const AABB& world) const
    {
        AABB result;
        for (int index = 0; index < 8; ++index) {
            const Vector3 corner{
                (index & 1) ? world.max.x : world.min.x,
                (index & 2) ? world.max.y : world.min.y,
                (index & 4) ? world.max.z : world.min.z,
            };
            result = result.Including(ToLocal(corner));
        }
        return result;
    }

    AABB MeshView::WorldBounds() const
    {
        if (mesh == nullptr) {
            return AABB();
        }
        const AABB& local = mesh->Bounds();
        AABB result;
        for (int index = 0; index < 8; ++index) {
            const Vector3 corner{
                (index & 1) ? local.max.x : local.min.x,
                (index & 2) ? local.max.y : local.min.y,
                (index & 4) ? local.max.z : local.min.z,
            };
            result = result.Including(ToWorld(corner));
        }
        return result;
    }

    //================================================
    // 三角形ごとの接触
    //================================================

    int CollectMeshContacts(const Sphere& sphere, const MeshView& view, Contact* outContacts, int maxContacts,
                            MeshContactMode mode)
    {
        if (!view.IsValid() || outContacts == nullptr || maxContacts <= 0 || !(sphere.radius > 0.0f)) {
            return 0;
        }
        const float flatCosine = std::cos(view.flatEdgeAngle);
        const AABB query = view.LocalBoxOf(BoxAround(sphere.center, sphere.center, sphere.radius));

        CandidateList list;
        view.mesh->ForEachTriangleInBox(query, [&](uint32_t index) {
            const WorldTriangle triangle = MakeWorldTriangle(view, index);
            if (!triangle.valid) {
                return;
            }
            const float planeDistance = Dot(sphere.center - triangle.corners[0], triangle.front);
            if (view.oneSided && planeDistance < 0.0f) {
                return;
            }
            const float side = (planeDistance >= 0.0f) ? 1.0f : -1.0f;
            const TrianglePoint onTriangle = ClosestPointOnTriangle(sphere.center,
                triangle.corners[0], triangle.corners[1], triangle.corners[2]);
            AddRoundContact(view, triangle, sphere.center, onTriangle, sphere.radius,
                side, side * planeDistance, flatCosine, list);
        });
        return Finish(list, sphere.radius * 0.25f, mode, outContacts, maxContacts);
    }

    int CollectMeshContacts(const Capsule& capsule, const MeshView& view, Contact* outContacts, int maxContacts,
                            MeshContactMode mode)
    {
        if (!view.IsValid() || outContacts == nullptr || maxContacts <= 0 || !(capsule.radius > 0.0f)) {
            return 0;
        }
        const float flatCosine = std::cos(view.flatEdgeAngle);
        const Vector3 low{
            (std::min)(capsule.start.x, capsule.end.x), (std::min)(capsule.start.y, capsule.end.y),
            (std::min)(capsule.start.z, capsule.end.z) };
        const Vector3 high{
            (std::max)(capsule.start.x, capsule.end.x), (std::max)(capsule.start.y, capsule.end.y),
            (std::max)(capsule.start.z, capsule.end.z) };
        const AABB query = view.LocalBoxOf(BoxAround(low, high, capsule.radius));

        CandidateList list;
        view.mesh->ForEachTriangleInBox(query, [&](uint32_t index) {
            const WorldTriangle triangle = MakeWorldTriangle(view, index);
            if (!triangle.valid) {
                return;
            }
            const Vector3& a = triangle.corners[0];
            const float startDistance = Dot(capsule.start - a, triangle.front);
            const float endDistance = Dot(capsule.end - a, triangle.front);
            // 片面は芯の両端が表にあるときだけ当てる（裏から入りかけた物を表へ押し出さない）
            if (view.oneSided && (std::min)(startDistance, endDistance) < 0.0f) {
                return;
            }

            Vector3 onSegment{};
            TrianglePoint onTriangle;
            const bool pierced = ClosestPointsSegmentTriangle(capsule.start, capsule.end,
                triangle.corners[0], triangle.corners[1], triangle.corners[2], onSegment, onTriangle);

            if (pierced) {
                // 芯が面を突き抜けている：中点の側へ、反対側の端が半径ぶん出るまで押す
                const float middle = (startDistance + endDistance) * 0.5f;
                const float side = (middle >= 0.0f) ? 1.0f : -1.0f;
                RawContact contact;
                contact.kind = ContactKind::Face;
                contact.push = triangle.front * side;
                contact.depth = capsule.radius - (std::min)(side * startDistance, side * endDistance);
                contact.point = onSegment;
                SetFaceVertices(view, index, contact);
                list.Add(contact);
                return;
            }

            const float planeDistance = Dot(onSegment - a, triangle.front);
            float side = 1.0f;
            if (std::abs(planeDistance) > kTiny) {
                side = (planeDistance > 0.0f) ? 1.0f : -1.0f;
            }
            else {
                side = (startDistance + endDistance >= 0.0f) ? 1.0f : -1.0f;
            }
            AddRoundContact(view, triangle, onSegment, onTriangle, capsule.radius,
                side, side * planeDistance, flatCosine, list);

            // 寝かせたカプセルを両端で支えるよう、同じ側にある両端の球も調べる
            if (mode == MeshContactMode::Manifold) {
                const float endDistances[2] = { startDistance, endDistance };
                const Vector3* const ends[2] = { &capsule.start, &capsule.end };
                for (int end = 0; end < 2; ++end) {
                    if (side * endDistances[end] < 0.0f) {
                        continue;
                    }
                    const TrianglePoint onEnd = ClosestPointOnTriangle(*ends[end],
                        triangle.corners[0], triangle.corners[1], triangle.corners[2]);
                    AddRoundContact(view, triangle, *ends[end], onEnd, capsule.radius,
                        side, side * endDistances[end], flatCosine, list);
                }
            }
        });
        return Finish(list, capsule.radius * 0.25f, mode, outContacts, maxContacts);
    }

    int CollectMeshContacts(const OBB& box, const MeshView& view, Contact* outContacts, int maxContacts,
                            MeshContactMode mode)
    {
        if (!view.IsValid() || outContacts == nullptr || maxContacts <= 0) {
            return 0;
        }
        const float flatCosine = std::cos(view.flatEdgeAngle);
        const AABB query = view.LocalBoxOf(box.ToAABB());

        CandidateList list;
        view.mesh->ForEachTriangleInBox(query, [&](uint32_t index) {
            const WorldTriangle triangle = MakeWorldTriangle(view, index);
            if (!triangle.valid) {
                return;
            }
            const float planeDistance = Dot(box.center - triangle.corners[0], triangle.front);
            if (view.oneSided && planeDistance < 0.0f) {
                return;
            }
            const float side = (planeDistance >= 0.0f) ? 1.0f : -1.0f;
            AddBoxContact(view, triangle, box, side, flatCosine, mode, list);
        });
        const float smallest = (std::min)({ box.halfExtents.x, box.halfExtents.y, box.halfExtents.z });
        return Finish(list, smallest * 0.5f, mode, outContacts, maxContacts);
    }

    //================================================
    // 代表の接触
    //================================================

    bool Intersect(const Sphere& sphere, const MeshView& mesh, Contact* outContact)
    {
        return IntersectShape(sphere, mesh, outContact);
    }

    bool Intersect(const MeshView& mesh, const Sphere& sphere, Contact* outContact)
    {
        return IntersectMeshFirst(mesh, sphere, outContact);
    }

    bool Intersect(const Capsule& capsule, const MeshView& mesh, Contact* outContact)
    {
        return IntersectShape(capsule, mesh, outContact);
    }

    bool Intersect(const MeshView& mesh, const Capsule& capsule, Contact* outContact)
    {
        return IntersectMeshFirst(mesh, capsule, outContact);
    }

    bool Intersect(const OBB& box, const MeshView& mesh, Contact* outContact)
    {
        return IntersectShape(box, mesh, outContact);
    }

    bool Intersect(const MeshView& mesh, const OBB& box, Contact* outContact)
    {
        return IntersectMeshFirst(mesh, box, outContact);
    }

    bool Intersect(const MeshView&, const MeshView&, Contact*)
    {
        return false;
    }

    //================================================
    // レイ
    //================================================

    bool Raycast(const Ray& ray, const MeshView& mesh, RayHit* outHit, float tMin, float tMax)
    {
        if (!mesh.IsValid()) {
            return false;
        }
        // 移し方は線形なので、ローカルの t はワールドの t と同じ
        const Ray local(mesh.ToLocal(ray.origin), mesh.DirectionToLocal(ray.direction));
        RayHit hit{};
        if (!mesh.mesh->Raycast(local, tMin, tMax, mesh.oneSided, &hit, nullptr)) {
            return false;
        }
        if (outHit) {
            Vector3 normal = mesh.NormalToWorld(hit.normal);
            if (Dot(normal, ray.direction) > 0.0f) {
                normal = normal * -1.0f;
            }
            outHit->distance = hit.distance;
            outHit->point = ray.origin + ray.direction * hit.distance;
            outHit->normal = normal;
        }
        return true;
    }
}
}
