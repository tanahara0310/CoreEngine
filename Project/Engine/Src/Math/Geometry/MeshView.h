#pragma once

#include "Math/Geometry/Intersect.h"
#include "Math/Geometry/RayCast.h"
#include "Math/Geometry/TriangleMesh.h"

#include <cmath>

/// @file
/// @brief 三角形の集まりをワールドに置いた見え方と、形との判定
/// @details 三角形の集まりはローカルのまま共有し、ここで持つ位置・向き・拡大縮小でワールドへ移す。
///          問い合わせの形はローカルの箱で三角形を絞り、絞った三角形だけをワールドへ移して調べる。

namespace CoreEngine
{
namespace Geometry
{
    /// @brief ワールドに置いた三角形の集まり
    /// @details ワールドの点 = origin + axes[0] * (x * scale.x) + axes[1] * (y * scale.y) + axes[2] * (z * scale.z)
    struct MeshView {
        const TriangleMesh* mesh = nullptr;
        Vector3 origin{ 0.0f, 0.0f, 0.0f };
        Vector3 axes[3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };  ///< 正規直交
        Vector3 scale{ 1.0f, 1.0f, 1.0f };   ///< 軸ごとの倍率（負なら鏡写し）
        bool    oneSided = false;            ///< true なら表からだけ当たる（芯が全部表にある形だけ。芯は球と箱が中心・カプセルが線分）
        float   flatEdgeAngle = 0.0175f;     ///< 平らとみなす縁のなす角（ラジアン）

        /// @brief 判定に使えるか（三角形があり、倍率が 0 の軸が無い）
        bool IsValid() const
        {
            constexpr float kMinScale = 1e-6f;
            return mesh != nullptr
                && std::abs(scale.x) > kMinScale && std::abs(scale.y) > kMinScale && std::abs(scale.z) > kMinScale;
        }

        /// @brief 鏡写しか（倍率の符号の積が負）
        bool Mirrored() const { return scale.x * scale.y * scale.z < 0.0f; }

        Vector3 ToWorld(const Vector3& local) const
        {
            return origin + axes[0] * (local.x * scale.x) + axes[1] * (local.y * scale.y) + axes[2] * (local.z * scale.z);
        }

        Vector3 ToLocal(const Vector3& world) const
        {
            const Vector3 d = world - origin;
            return { Dot(d, axes[0]) / scale.x, Dot(d, axes[1]) / scale.y, Dot(d, axes[2]) / scale.z };
        }

        /// @brief ワールドの向きをローカルへ（長さは倍率で変わる）
        Vector3 DirectionToLocal(const Vector3& world) const
        {
            return { Dot(world, axes[0]) / scale.x, Dot(world, axes[1]) / scale.y, Dot(world, axes[2]) / scale.z };
        }

        /// @brief ローカルの面の向きをワールドへ（正規化する）
        Vector3 NormalToWorld(const Vector3& local) const
        {
            return Normalize(axes[0] * (local.x / scale.x) + axes[1] * (local.y / scale.y) + axes[2] * (local.z / scale.z));
        }

        /// @brief ワールドの箱を包むローカルの箱
        AABB LocalBoxOf(const AABB& world) const;

        /// @brief 三角形の集まり全体を包むワールドの箱
        AABB WorldBounds() const;

        /// @brief 三角形の 3 頂点（ワールド）
        void WorldTriangle(uint32_t triangle, Vector3& a, Vector3& b, Vector3& c) const
        {
            mesh->Corners(triangle, a, b, c);
            a = ToWorld(a);
            b = ToWorld(b);
            c = ToWorld(c);
        }
    };

    /// @brief メッシュの接触の集め方
    enum class MeshContactMode : uint8_t {
        Deepest,   ///< 三角形ごとに代表の点 1 つ。深い順に上限まで残す
        Manifold,  ///< 剛体を支える点。箱は面の下に入った箱の頂点と、三角形のうち箱の中に入った部分の頂点、
                   ///< カプセルは両端の球の接触も集める。上限を超えたら、向きの違う面を 1 つずつ残してから、
                   ///< 互いに離れた点を残す
    };

    /// @brief 形とメッシュの接触を三角形ごとに集める
    /// @param outContacts 書き出し先。法線は形からメッシュへ向かう向き
    ///                    （形を法線の逆へ depth 動かすとその三角形から離れる）
    /// @param maxContacts 書き出せる数の上限
    /// @param mode        上限より多いときの残し方。Deepest は深い順に並ぶ。Manifold は先頭がいちばん深い
    /// @return 書き出した数
    /// @details 継ぎ目の縁や頂点で引っかからないよう、平ら・へこみの縁と内側の頂点では面の向きを法線にする。
    ///          向きがほぼ同じで近い接触は 1 つにまとめる。
    int CollectMeshContacts(const Sphere& sphere, const MeshView& mesh, Contact* outContacts, int maxContacts,
                            MeshContactMode mode = MeshContactMode::Deepest);
    int CollectMeshContacts(const Capsule& capsule, const MeshView& mesh, Contact* outContacts, int maxContacts,
                            MeshContactMode mode = MeshContactMode::Deepest);
    int CollectMeshContacts(const OBB& box, const MeshView& mesh, Contact* outContacts, int maxContacts,
                            MeshContactMode mode = MeshContactMode::Deepest);

    /// @note 代表の接触は三角形ごとの接触のうちいちばん深いもの。法線は A から B へ。
    bool Intersect(const Sphere& sphere, const MeshView& mesh, Contact* outContact = nullptr);
    bool Intersect(const MeshView& mesh, const Sphere& sphere, Contact* outContact = nullptr);
    bool Intersect(const Capsule& capsule, const MeshView& mesh, Contact* outContact = nullptr);
    bool Intersect(const MeshView& mesh, const Capsule& capsule, Contact* outContact = nullptr);
    bool Intersect(const OBB& box, const MeshView& mesh, Contact* outContact = nullptr);
    bool Intersect(const MeshView& mesh, const OBB& box, Contact* outContact = nullptr);

    /// @note メッシュどうしは当てない（常に false）
    bool Intersect(const MeshView& a, const MeshView& b, Contact* outContact = nullptr);

    /// @note 片面なら表から当たる三角形だけ。法線はレイ側を向く。
    bool Raycast(const Ray& ray, const MeshView& mesh, RayHit* outHit = nullptr,
                 float tMin = 0.0f, float tMax = kRayMaxDistance);
}
}
