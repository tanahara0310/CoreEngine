#include "pch.h"

#include "Collision/Mesh/CollisionMeshLibrary.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/GameObject.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelResource.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_set>

/// @file
/// @brief メッシュの形の三角形を描画のモデルから取り出す口（起動時に CollisionMeshLibrary へ差し込む）

namespace CoreEngine
{
    namespace {
        /// @brief 読み込みを頼んだモデルのファイル（同じファイルを何度も頼まない）
        std::unordered_set<std::string>& RequestedFiles()
        {
            static std::unordered_set<std::string> files;
            return files;
        }

        std::mutex& RequestMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        /// @brief モデルのリソースが三角形の出どころに使えるか
        CollisionMeshSourceInfo Inspect(const ModelResource* resource)
        {
            if (resource == nullptr) {
                return { nullptr, CollisionMeshStatus::NoModel };
            }
            if (!resource->IsLoaded()) {
                return { nullptr, CollisionMeshStatus::Loading };
            }
            const ModelData& data = resource->GetModelData();
            if (!data.skinClusterData.empty()) {
                return { nullptr, CollisionMeshStatus::Skinned };
            }
            if (data.vertices.empty() || data.indices.empty()) {
                return { nullptr, CollisionMeshStatus::Empty };
            }
            return { resource, CollisionMeshStatus::Ready };
        }

        CollisionMeshSourceInfo Identify(const GameObject& owner, const CollisionShape& shape)
        {
            if (shape.meshSource == CollisionMeshSource::File) {
                if (shape.meshModel.empty()) {
                    return { nullptr, CollisionMeshStatus::NoModel };
                }
                EngineSystem* const engine = owner.GetEngineSystem();
                ModelManager* const models = engine ? engine->GetService<ModelManager>() : nullptr;
                if (models == nullptr) {
                    return { nullptr, CollisionMeshStatus::NoModel };
                }
                if (ModelResource* const resource = models->GetModelResource(shape.meshModel)) {
                    return Inspect(resource);
                }
                // まだ読み込んでいないファイルは、裏で読み込みを始めて待つ
                bool first = false;
                {
                    std::lock_guard<std::mutex> lock(RequestMutex());
                    first = RequestedFiles().insert(shape.meshModel).second;
                }
                if (first) {
                    models->BeginPreload({ shape.meshModel });
                }
                return { nullptr, CollisionMeshStatus::Loading };
            }

            const MeshRendererComponent* const renderer = owner.GetComponent<MeshRendererComponent>();
            if (renderer == nullptr) {
                return { nullptr, CollisionMeshStatus::NoModel };
            }
            const Model* const model = renderer->GetModel();
            if (model == nullptr) {
                return { nullptr, renderer->GetModelPath().empty() ? CollisionMeshStatus::NoModel
                                                                   : CollisionMeshStatus::Loading };
            }
            if (model->HasSkinCluster()) {
                return { nullptr, CollisionMeshStatus::Skinned };
            }
            return Inspect(model->GetModelResource());
        }

        bool Fill(const void* key, int lod, Geometry::TriangleMeshInput& out)
        {
            const ModelResource* const resource = static_cast<const ModelResource*>(key);
            if (resource == nullptr || !resource->IsLoaded()) {
                return false;
            }
            const ModelData& data = resource->GetModelData();

            out.positions.resize(data.vertices.size());
            out.normals.resize(data.vertices.size());
            for (size_t i = 0; i < data.vertices.size(); ++i) {
                const VertexData& vertex = data.vertices[i];
                out.positions[i] = Vector3{ vertex.position.x, vertex.position.y, vertex.position.z };
                out.normals[i] = vertex.normal;
            }

            const uint32_t indexCount = static_cast<uint32_t>(data.indices.size());
            const uint32_t level = static_cast<uint32_t>((std::max)(lod, 0));
            out.indices.clear();
            const auto appendRange = [&](uint32_t start, uint32_t count) {
                const uint32_t begin = (std::min)(start, indexCount);
                const uint32_t end = (std::min)(start + count, indexCount);
                for (uint32_t i = begin; i + 2 < end; i += 3) {
                    out.indices.push_back(static_cast<uint32_t>(data.indices[i + 0]));
                    out.indices.push_back(static_cast<uint32_t>(data.indices[i + 1]));
                    out.indices.push_back(static_cast<uint32_t>(data.indices[i + 2]));
                }
            };
            if (data.subMeshes.empty()) {
                appendRange(0, indexCount);
            }
            for (const SubMeshData& subMesh : data.subMeshes) {
                const SubMeshLodRange& range = subMesh.GetLod(level);
                appendRange(range.startIndex, range.indexCount);
            }
            return !out.indices.empty();
        }

        /// @brief 起動時に取り出し口を差し込む
        const bool kRegistered = [] {
            CollisionMeshLibrary::SetResolver(&Identify, &Fill);
            return true;
        }();
    }
}
