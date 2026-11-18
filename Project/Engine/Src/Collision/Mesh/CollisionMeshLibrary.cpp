#include "pch.h"
#include "CollisionMeshLibrary.h"

#include "Utility/Logger/Logger.h"

#include <map>
#include <mutex>
#include <utility>

namespace CoreEngine
{
    namespace {
        /// @brief 差し込まれた関数と控え
        struct LibraryState {
            CollisionMeshLibrary::IdentifyFunction identify;
            CollisionMeshLibrary::FillFunction     fill;
            /// 出どころと LOD の段 → 作った三角形の集まり（作れなかった出どころは nullptr を控える）
            std::map<std::pair<const void*, int>, std::shared_ptr<const Geometry::TriangleMesh>> meshes;
            std::mutex mutex;
        };

        LibraryState& State()
        {
            static LibraryState state;
            return state;
        }
    }

    const char* ToDisplayText(CollisionMeshStatus status)
    {
        switch (status) {
        case CollisionMeshStatus::Ready:   return "使えます";
        case CollisionMeshStatus::Loading: return "モデルの読み込み待ち";
        case CollisionMeshStatus::NoModel: return "モデルがありません";
        case CollisionMeshStatus::Skinned: return "骨入りのモデルはメッシュの当たりに使えません";
        case CollisionMeshStatus::Empty:   return "使える三角形がありません";
        case CollisionMeshStatus::None:
        default:                           return "";
        }
    }

    void CollisionMeshLibrary::SetResolver(IdentifyFunction identify, FillFunction fill)
    {
        LibraryState& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.identify = std::move(identify);
        state.fill = std::move(fill);
    }

    CollisionMeshSourceInfo CollisionMeshLibrary::Identify(const GameObject& owner, const CollisionShape& shape)
    {
        LibraryState& state = State();
        IdentifyFunction identify;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            identify = state.identify;
        }
        if (!identify) {
            return {};
        }
        return identify(owner, shape);
    }

    CollisionMeshLibrary::Result CollisionMeshLibrary::Acquire(const void* key, int lod)
    {
        Result result;
        if (key == nullptr) {
            return result;
        }

        LibraryState& state = State();
        const std::pair<const void*, int> cacheKey{ key, (std::max)(lod, 0) };
        FillFunction fill;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (const auto found = state.meshes.find(cacheKey); found != state.meshes.end()) {
                result.mesh = found->second;
                result.status = result.mesh ? CollisionMeshStatus::Ready : CollisionMeshStatus::Empty;
                return result;
            }
            fill = state.fill;
        }
        if (!fill) {
            return result;
        }

        Geometry::TriangleMeshInput input;
        if (!fill(key, cacheKey.second, input)) {
            return result;
        }
        std::shared_ptr<const Geometry::TriangleMesh> mesh = Geometry::TriangleMesh::Build(input);
        if (mesh) {
            const Geometry::TriangleMeshStats& stats = mesh->Stats();
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                "当たりのメッシュを作りました（三角形 {}・外した三角形 {}・{:.1f} ms）",
                stats.triangles, stats.removedTriangles, stats.buildMilliseconds);
        }

        std::lock_guard<std::mutex> lock(state.mutex);
        const auto [entry, inserted] = state.meshes.emplace(cacheKey, std::move(mesh));
        result.mesh = entry->second;
        result.status = result.mesh ? CollisionMeshStatus::Ready : CollisionMeshStatus::Empty;
        return result;
    }

    void CollisionMeshLibrary::Clear()
    {
        LibraryState& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.meshes.clear();
    }

    size_t CollisionMeshLibrary::CachedCount()
    {
        LibraryState& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        return state.meshes.size();
    }
}
