#include "pch.h"
#include "RTScene.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Model/VertexData.h"
#include "Graphics/RHI/Command/DeferredReleaseQueue.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/CVar/CVar.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        CVar<int> cvGeometryLod{ "r.RT.GeometryLod", 0,
            "レイトレーシング（影・水面の反射と屈折・コースティクス）の形に使うモデルの LOD の段。"
            "0 は元の形。モデルが持つ段より大きいときは最後の段（変えると BLAS を作り直す）",
            CVarRange{ 0.0f, static_cast<float>(SubMeshData::kMaxLodCount - 1) } };

        /// @brief 登録された出どころ 1 つ
        struct SourceEntry {
            const char* name = "";
            RTScene::SourceFunction function = nullptr;
        };

        /// @brief 登録された出どころ（静的初期化の順に左右されないよう関数の中に置く）
        std::vector<SourceEntry>& Sources()
        {
            static std::vector<SourceEntry> sources;
            return sources;
        }

        /// @brief BLAS の形 1 つぶんの材質をヒットシェーディングのサブメッシュ表の行へ詰める
        /// @param range             形の索引の範囲
        /// @param materialIndex     形の材質のスロット
        /// @param material          そのスロットの材質（無ければ既定値）
        /// @param baseColorOverride 全サブメッシュのベースカラーを差し替えるテクスチャ（無ければ ptr = 0）
        RTHitSubMesh MakeHitSubMesh(
            const AccelerationStructureManager::GeometryRange& range,
            uint32_t materialIndex,
            const MaterialInstance* material,
            const ModelResource& resource,
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorOverride,
            const DescriptorAllocator& descriptors)
        {
            RTHitSubMesh row{};
            row.firstTriangle = range.startIndex / 3;
            row.triangleCount = range.indexCount / 3;
            row.baseColorTextureIndex = kRTHitNoTexture;
            row.emissiveTextureIndex = kRTHitNoTexture;
            row.metallicRoughnessTextureIndex = kRTHitNoTexture;
            row.normalTextureIndex = kRTHitNoTexture;
            row.occlusionTextureIndex = kRTHitNoTexture;
            row.occlusionStrength = 0.0f;

            D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture = baseColorOverride;
            // 差し替えのテクスチャは透明を持ちうるものとして扱う
            bool baseColorMayHaveAlpha = (baseColorOverride.ptr != 0);
            D3D12_GPU_DESCRIPTOR_HANDLE normalTexture{};
            if (materialIndex < resource.GetMaterials().size()) {
                const ModelResource::PBRTextureHandles& textures = resource.GetMaterialTextures(materialIndex);
                if (baseColorTexture.ptr == 0) {
                    baseColorTexture = textures.baseColor;
                    baseColorMayHaveAlpha = textures.baseColorMayHaveAlpha;
                }
                if (textures.hasEmissive && textures.emissive.ptr != 0) {
                    row.emissiveTextureIndex = descriptors.GetSRVHeapIndex(textures.emissive);
                }
                if (textures.hasMetallicRoughness && textures.metallicRoughness.ptr != 0) {
                    row.metallicRoughnessTextureIndex = descriptors.GetSRVHeapIndex(textures.metallicRoughness);
                }
                if (textures.hasNormal) {
                    normalTexture = textures.normal;
                }
                if (textures.hasOcclusion && textures.occlusion.ptr != 0) {
                    row.occlusionTextureIndex = descriptors.GetSRVHeapIndex(textures.occlusion);
                }
            }
            if (baseColorTexture.ptr != 0) {
                row.baseColorTextureIndex = descriptors.GetSRVHeapIndex(baseColorTexture);
            }

            row.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            row.uvTransformU = { 1.0f, 0.0f, 0.0f, 0.0f };
            row.uvTransformV = { 0.0f, 1.0f, 0.0f, 0.0f };
            row.roughness = 0.5f;
            row.alphaCutoff = 0.5f;
            row.flags = kRTHitSubMeshFlagLit;
            if (material) {
                // HLSL 側は mul(float4(uv, 0, 1), uvTransform)
                const Matrix4x4 uvTransform = material->GetUVTransform();
                row.baseColor = material->GetColor();
                row.uvTransformU = { uvTransform.m[0][0], uvTransform.m[1][0], uvTransform.m[3][0], 0.0f };
                row.uvTransformV = { uvTransform.m[0][1], uvTransform.m[1][1], uvTransform.m[3][1], 0.0f };
                row.emissive = material->GetEmissiveFactor();
                row.metallic = material->GetMetallic();
                row.roughness = material->GetRoughness();
                row.alphaCutoff = material->GetAlphaCutoff();
                row.occlusionStrength = material->GetOcclusionStrength();
                row.flags = (material->IsLightingEnabled() ? kRTHitSubMeshFlagLit : 0u)
                    | (material->IsDitheringEnabled() ? kRTHitSubMeshFlagDither : 0u);
                // 法線テクスチャは材質が法線マップを使うときだけ（GBuffer.PS と同じ）
                if (material->IsNormalMapEnabled() && normalTexture.ptr != 0) {
                    row.normalTextureIndex = descriptors.GetSRVHeapIndex(normalTexture);
                }
            }
            // 抜く判定はラスタと同じく「ベースカラーのアルファ ≦ しきい値」（ディザの材質は 0.5）
            const float alphaThreshold = ((row.flags & kRTHitSubMeshFlagDither) != 0) ? 0.5f : row.alphaCutoff;
            const bool textureMayCut = baseColorMayHaveAlpha && row.baseColorTextureIndex != kRTHitNoTexture;
            if (textureMayCut || row.baseColor.w <= alphaThreshold) {
                row.flags |= kRTHitSubMeshFlagAlphaTest;
            }
            return row;
        }

        // ヒットシェーディング（Include/RayTracing/RTHitShading.hlsli）は頂点を VertexData の並びで読む
        static_assert(sizeof(VertexData) == 64 && offsetof(VertexData, texcoord) == 16
            && offsetof(VertexData, normal) == 24 && offsetof(VertexData, tangent) == 36,
            "RTHitShading.hlsli の kRTHitVertexStride / kRTHitVertex*Offset と一致させること");

        /// @brief TLAS のインスタンス 1 つぶんの行と、その形（サブメッシュ）の行を足す
        /// @param geometries BLAS の形の並び（サブメッシュの行も同じ並びで足す）
        /// @param model      材質の持ち主（無ければモデルリソースの既定の材質）
        /// @return アルファで抜ける所がありうる形を 1 つでも持つか
        bool AppendHitInstance(
            const AccelerationStructureManager::InstanceDesc& instance,
            const AccelerationStructureManager::StaticBLAS& blas,
            std::span<const AccelerationStructureManager::GeometryRange> geometries,
            const ModelResource& resource,
            const Model* model,
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorOverride,
            const DescriptorAllocator& descriptors,
            std::vector<RTHitInstance>& outInstances,
            std::vector<RTHitSubMesh>& outSubMeshes)
        {
            RTHitInstance row{};
            for (int r = 0; r < 3; ++r) {
                row.objectToWorld[r] = {
                    instance.transform[r][0], instance.transform[r][1],
                    instance.transform[r][2], instance.transform[r][3] };
            }
            row.vertexBufferIndex = blas.vertexBufferIndex;
            row.indexBufferIndex = blas.indexBufferIndex;
            row.firstSubMesh = static_cast<uint32_t>(outSubMeshes.size());

            const std::vector<SubMeshData>& subMeshes = resource.GetSubMeshes();
            const uint32_t flagsToAdd = (model && model->HasSkinCluster()) ? kRTHitSubMeshFlagMoving : 0u;
            bool alphaTested = false;
            for (const AccelerationStructureManager::GeometryRange& range : geometries) {
                const uint32_t materialIndex = (range.subMeshIndex < subMeshes.size())
                    ? subMeshes[range.subMeshIndex].materialIndex : 0u;
                const MaterialInstance* material = model
                    ? model->GetMaterial(materialIndex)
                    : resource.GetDefaultMaterial(materialIndex);
                RTHitSubMesh subMeshRow =
                    MakeHitSubMesh(range, materialIndex, material, resource, baseColorOverride, descriptors);
                subMeshRow.flags |= flagsToAdd;
                alphaTested = alphaTested || (subMeshRow.flags & kRTHitSubMeshFlagAlphaTest) != 0;
                outSubMeshes.push_back(subMeshRow);
            }
            row.subMeshCount = static_cast<uint32_t>(outSubMeshes.size()) - row.firstSubMesh;
            outInstances.push_back(row);
            return alphaTested;
        }
    }

    // ──────────────────────────────────────────────────────────
    // Builder
    // ──────────────────────────────────────────────────────────

    size_t RTScene::Builder::Add(const Instance& instance)
    {
        if (instance.mask == 0 || !instance.resource || !instance.resource->IsLoaded()) {
            return kNoRow;
        }
        if (instance.resource != lastResource_) {
            lastResource_ = instance.resource;
            AccelerationStructureManager::CollectGeometryRanges(*instance.resource, scene_.geometryLod_, lastGeometries_);
            lastBLAS_ = asMgr_.AcquireStaticBLAS(frame_.cmdList, *instance.resource, lastGeometries_, frame_.frame);
        }
        if (!lastBLAS_) {
            return kNoRow;
        }

        AccelerationStructureManager::InstanceDesc desc;
        desc.blasAddress = lastBLAS_->address;
        desc.SetTransform(instance.world);
        desc.instanceMask = instance.mask;

        bool alphaTested = false;
        if (scene_.descriptorAllocator_) {
            alphaTested = AppendHitInstance(desc, *lastBLAS_, lastGeometries_, *instance.resource, instance.model,
                instance.baseColorOverride, *scene_.descriptorAllocator_, scene_.hitInstances_, scene_.hitSubMeshes_);
        }
        // 抜ける所の無い物は、どのレイにも不透明な物として当てる（シェーダーへ判定を返さない）
        if (!alphaTested) {
            desc.flags = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
        }

        const size_t row = scene_.tlasInstances_.size();
        scene_.tlasInstances_.push_back(desc);
        scene_.rowResources_.push_back(instance.resource);
        return row;
    }

    void RTScene::Builder::SetDynamicShape(size_t row, DynamicShape shape)
    {
        if (row >= scene_.tlasInstances_.size() || !shape.owner) {
            return;
        }
        scene_.dynamicShapes_.push_back(PendingDynamicShape{ row, std::move(shape) });
    }

    bool RTScene::Builder::TrackSolidMotion(const void* owner, const Matrix4x4& world)
    {
        return scene_.TrackSolidMotion(owner, world, frame_.frame);
    }

    bool RTScene::Builder::HasDynamicShape(const void* owner) const
    {
        return asMgr_.HasDynamicBLAS(owner);
    }

    // ──────────────────────────────────────────────────────────
    // RTScene
    // ──────────────────────────────────────────────────────────

    void RTScene::RegisterSource(const char* name, SourceFunction function)
    {
        if (!function) {
            return;
        }
        Sources().push_back(SourceEntry{ name ? name : "", function });
    }

    void RTScene::Initialize(ID3D12Device* device, DescriptorAllocator* descriptorAllocator)
    {
        device_ = device;
        descriptorAllocator_ = descriptorAllocator;
    }

    void RTScene::Build(const FrameInput& input, GraphicsCore& graphics, AccelerationStructureManager& asMgr)
    {
        hitTablesValid_ = false;
        tlasInstances_.clear();
        rowResources_.clear();
        hitInstances_.clear();
        hitSubMeshes_.clear();
        dynamicShapes_.clear();

        geometryLod_ = static_cast<uint32_t>(std::clamp(cvGeometryLod.Get(), 0, static_cast<int>(SubMeshData::kMaxLodCount - 1)));

        const VertexAnimationParams animParams = VertexAnimationParams::Build();
        Builder builder(*this, asMgr, input, animParams);

        const std::vector<SourceEntry>& sources = Sources();
        sourceStats_.resize(sources.size());
        for (size_t i = 0; i < sources.size(); ++i) {
            const size_t before = tlasInstances_.size();
            sources[i].function(builder);
            sourceStats_[i].name = sources[i].name;
            sourceStats_[i].instanceCount = static_cast<uint32_t>(tlasInstances_.size() - before);
        }

        BuildDynamicShapes(input, animParams, asMgr);
        staticSolidSignature_ = ComputeStaticSolidSignature();

        // 物が 0 個のフレームも組む（下で回収する動的 BLAS を前の TLAS から撃たせない）
        asMgr.BuildTLAS(input.cmdList, tlasInstances_);
        if (!tlasInstances_.empty() && asMgr.IsTLASReady()) {
            UploadHitTables(graphics, asMgr);
        }

        // 使われなくなったモデルの BLAS と、消えた・動かなくなった持ち主の動的 BLAS と頂点バッファを捨てる
        // （GPU が使い終わってから解放）
        asMgr.RetireUnusedStaticBLAS(input.frame);
        asMgr.RetireUnusedDynamicBLAS(input.frame);
        RetireUnusedSolidMotion(input.frame);
        if (input.deformer) {
            input.deformer->RetireUnused(input.frame);
        }
    }

    void RTScene::BuildDynamicShapes(const FrameInput& input, const VertexAnimationParams& animParams,
        AccelerationStructureManager& asMgr)
    {
        if (dynamicShapes_.empty()) {
            return;
        }

        // 変形を掛ける形をまとめて 1 回で変形する
        std::vector<VertexAnimationDeformer::Request> requests;
        std::vector<int> deformIndices(dynamicShapes_.size(), -1);
        for (size_t i = 0; i < dynamicShapes_.size(); ++i) {
            if (dynamicShapes_[i].shape.deform) {
                deformIndices[i] = static_cast<int>(requests.size());
                requests.push_back(*dynamicShapes_[i].shape.deform);
            }
        }
        const std::vector<VertexAnimationDeformer::Output> deformed = (input.deformer && !requests.empty())
            ? input.deformer->DeformBatch(input.cmdList, requests, animParams, input.frame)
            : std::vector<VertexAnimationDeformer::Output>(requests.size());

        if (!asMgr.BeginDynamicBLASBatch(input.cmdList)) {
            return;
        }
        for (size_t i = 0; i < dynamicShapes_.size(); ++i) {
            const PendingDynamicShape& pending = dynamicShapes_[i];
            ID3D12Resource* vertices = pending.shape.vertices;
            uint32_t vertexBufferIndex = pending.shape.vertexBufferIndex;
            bool changed = pending.shape.changed;
            if (deformIndices[i] >= 0) {
                const VertexAnimationDeformer::Output& output = deformed[static_cast<size_t>(deformIndices[i])];
                if (output.vertices) {
                    vertices = output.vertices;
                    vertexBufferIndex = output.vertexBufferIndex;
                    changed = true;
                }
            }
            // 作れなければ静止形の BLAS のまま
            const ModelResource* resource = rowResources_[pending.row];
            if (!vertices || !resource) {
                continue;
            }
            // 形の並びは静止形の BLAS と同じ（サブメッシュの行をそのまま使う）
            AccelerationStructureManager::CollectGeometryRanges(*resource, geometryLod_, geometryScratch_);
            AccelerationStructureManager::BLASDesc desc{};
            desc.vertexBuffer = vertices;
            desc.vertexCount = resource->GetVertexCount();
            desc.vertexStride = sizeof(VertexData);  // スキニングの出力も頂点変形の出力も VertexData の並び（位置が先頭）
            desc.vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            desc.indexBuffer = resource->GetIndexBuffer();
            desc.indexFormat = DXGI_FORMAT_R32_UINT;
            desc.geometries = geometryScratch_;
            const D3D12_GPU_VIRTUAL_ADDRESS blas =
                asMgr.BuildOrUpdateDynamicBLAS(pending.shape.owner, desc, input.frame, changed);
            if (!blas) {
                continue;
            }
            tlasInstances_[pending.row].blasAddress = blas;
            // 表の行も、BLAS と同じ変形後の頂点を読む
            if (pending.row < hitInstances_.size() && vertexBufferIndex != UINT32_MAX) {
                hitInstances_[pending.row].vertexBufferIndex = vertexBufferIndex;
            }
        }
        asMgr.EndDynamicBLASBatch();
    }

    uint64_t RTScene::ComputeStaticSolidSignature() const
    {
        uint64_t signature = 14695981039346656037ull;
        auto mix = [&signature](const void* data, size_t bytes) {
            const auto* p = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < bytes; ++i) {
                signature = (signature ^ p[i]) * 1099511628211ull;
            }
        };
        for (const AccelerationStructureManager::InstanceDesc& inst : tlasInstances_) {
            if ((inst.instanceMask & RTInstanceMask::kStaticSolid) == 0) {
                continue;
            }
            mix(&inst.blasAddress, sizeof(inst.blasAddress));
            mix(inst.transform, sizeof(inst.transform));
        }
        return signature;
    }

    void RTScene::UploadHitTables(GraphicsCore& graphics, const AccelerationStructureManager& asMgr)
    {
        if (!device_ || !descriptorAllocator_ || hitInstances_.empty() || hitSubMeshes_.empty()
            || hitInstances_.size() != asMgr.GetTLASInstanceCount()) {
            return;
        }
        ringIndex_ = (ringIndex_ + 1) % kMaxFramesInFlight;
        hitTablesValid_ =
            UploadHitTable(graphics, hitInstanceTables_, hitInstances_.data(), static_cast<UINT>(hitInstances_.size()),
                sizeof(RTHitInstance), "RTHitInstanceTableSRV") &&
            UploadHitTable(graphics, hitSubMeshTables_, hitSubMeshes_.data(), static_cast<UINT>(hitSubMeshes_.size()),
                sizeof(RTHitSubMesh), "RTHitSubMeshTableSRV");
    }

    uint32_t RTScene::GetHitInstanceTableIndex() const
    {
        const DescriptorHandle& srv = hitInstanceTables_.srvs[ringIndex_];
        return (hitTablesValid_ && srv.IsValid()) ? srv.index : UINT32_MAX;
    }

    uint32_t RTScene::GetHitSubMeshTableIndex() const
    {
        const DescriptorHandle& srv = hitSubMeshTables_.srvs[ringIndex_];
        return (hitTablesValid_ && srv.IsValid()) ? srv.index : UINT32_MAX;
    }

    RTHitTableIndices RTScene::GetHitTableIndices() const
    {
        RTHitTableIndices indices;
        indices.instanceTable = GetHitInstanceTableIndex();
        indices.subMeshTable = GetHitSubMeshTableIndex();
        return indices;
    }

    bool RTScene::UploadHitTable(GraphicsCore& graphics, HitTableRing& ring, const void* data, UINT elementCount,
        UINT elementStride, const char* debugName)
    {
        // フレームごとに別のスロットへ書く（実行待ちのフレームが読んでいる表を書き換えない）
        const uint32_t slot = ringIndex_;
        auto& buffer = ring.buffers[slot];
        if (!buffer || ring.capacities[slot] < elementCount) {
            if (buffer) {
                graphics.DeferredRelease().PushForCurrentFrame(std::move(buffer));
            }
            const UINT capacity = (std::max)(elementCount + elementCount / 2, 64u);

            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC resDesc{};
            resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resDesc.Width = static_cast<UINT64>(capacity) * elementStride;
            resDesc.Height = 1;
            resDesc.DepthOrArraySize = 1;
            resDesc.MipLevels = 1;
            resDesc.SampleDesc.Count = 1;
            resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            HRESULT hr = device_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&buffer));
            if (FAILED(hr)) {
                ring.capacities[slot] = 0;
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                    "RTScene::UploadHitTable: failed to create {} buffer. hr=0x{:08X}",
                    debugName, static_cast<unsigned int>(hr));
                return false;
            }
            ring.capacities[slot] = capacity;

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = capacity;
            srvDesc.Buffer.StructureByteStride = elementStride;
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            descriptorAllocator_->EnsureSRV(ring.srvs[slot], buffer.Get(), srvDesc, debugName);
        }

        void* mapped = nullptr;
        if (FAILED(buffer->Map(0, nullptr, &mapped)) || !mapped) {
            return false;
        }
        std::memcpy(mapped, data, static_cast<size_t>(elementCount) * elementStride);
        buffer->Unmap(0, nullptr);
        return true;
    }

    bool RTScene::TrackSolidMotion(const void* owner, const Matrix4x4& world, uint64_t frame)
    {
        auto [it, inserted] = solidMotion_.try_emplace(owner);
        SolidMotionEntry& entry = it->second;
        entry.lastSeenFrame = frame;
        if (inserted) {
            entry.world = world;
            return false;
        }
        if (std::memcmp(&entry.world, &world, sizeof(Matrix4x4)) != 0) {
            entry.world = world;
            entry.lastMovedFrame = frame;
            entry.moved = true;
        }
        return entry.moved && frame - entry.lastMovedFrame <= kSolidMotionKeepFrames;
    }

    void RTScene::RetireUnusedSolidMotion(uint64_t frame)
    {
        for (auto it = solidMotion_.begin(); it != solidMotion_.end();) {
            if (it->second.lastSeenFrame != frame) {
                it = solidMotion_.erase(it);
            } else {
                ++it;
            }
        }
    }
}
