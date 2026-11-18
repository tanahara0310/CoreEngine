#pragma once

#include "Collision/CollisionShape.h"
#include "Math/Geometry/TriangleMesh.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

/// @file
/// @brief メッシュの形の三角形の取り出し口と、作った三角形の集まりの控え
/// @details Collision の層はモデルの型を知らない。三角形の出どころを探す関数と、
///          三角形を書き出す関数をモデルの側が差し込む。

namespace CoreEngine
{
    class GameObject;

    /// @brief メッシュの形の状態
    enum class CollisionMeshStatus : uint8_t {
        None,      ///< メッシュの形ではない・まだ調べていない
        Ready,     ///< 使える
        Loading,   ///< モデルの読み込み待ち
        NoModel,   ///< モデルが無い（メッシュレンダラーが無い・ファイルが空・読めない）
        Skinned,   ///< 骨入りのモデルなので使えない
        Empty,     ///< 使える三角形が無い
    };

    /// @brief 状態の説明（インスペクタと Console に出す）
    const char* ToDisplayText(CollisionMeshStatus status);

    /// @brief 三角形の出どころ
    struct CollisionMeshSourceInfo {
        const void*         key = nullptr;  ///< 控えの鍵（モデルのリソース）。使えなければ nullptr
        CollisionMeshStatus status = CollisionMeshStatus::NoModel;  ///< key が nullptr のときの理由
    };

    class CollisionMeshLibrary {
    public:
        /// @brief 持ち主と形から三角形の出どころを探す（毎フレーム呼ぶので軽くする）
        using IdentifyFunction = std::function<CollisionMeshSourceInfo(const GameObject& owner, const CollisionShape& shape)>;

        /// @brief 出どころの三角形を書き出す
        /// @return 書き出せたら true
        using FillFunction = std::function<bool(const void* key, int lod, Geometry::TriangleMeshInput& out)>;

        /// @brief 取り出し口を差し込む（モデルの側が起動時に 1 回）
        static void SetResolver(IdentifyFunction identify, FillFunction fill);

        /// @brief 三角形の出どころを探す
        static CollisionMeshSourceInfo Identify(const GameObject& owner, const CollisionShape& shape);

        /// @brief 取り出した結果
        struct Result {
            std::shared_ptr<const Geometry::TriangleMesh> mesh;
            CollisionMeshStatus status = CollisionMeshStatus::NoModel;
        };

        /// @brief 出どころと LOD の段の三角形の集まりを出す（控えに無ければ作って控える）
        static Result Acquire(const void* key, int lod);

        /// @brief 控えを全部捨てる
        static void Clear();

        /// @brief 控えている数
        static size_t CachedCount();
    };
}
