#include "pch.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "ModelResource.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Command/UploadContext.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Model/Derived/ModelDerivedData.h"
#include "Graphics/Model/Import/MeshImporter.h"
#include "Graphics/Model/Skeleton/SkeletonLoader.h"
#include "Graphics/Model/VertexData.h"
#include "Utility/Logger/Logger.h"
#include "EngineSystem/Startup/StartupProgress.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>


namespace CoreEngine
{
    void ModelResource::Initialize(GraphicsCore* dxCommon, TextureManager* textureMg)
    {
        dxCommon_ = dxCommon;
        textureManager_ = textureMg;
    }

    namespace
    {
        /// @brief テクスチャの形式が透明を持ちうるか
        /// @details BC1 はインポートで全画素が不透明な画像にだけ選ぶので、透明を持たない形式として扱う
        bool FormatMayHaveAlpha(DXGI_FORMAT format)
        {
            switch (format) {
            case DXGI_FORMAT_BC1_TYPELESS:
            case DXGI_FORMAT_BC1_UNORM:
            case DXGI_FORMAT_BC1_UNORM_SRGB:
            case DXGI_FORMAT_BC4_TYPELESS:
            case DXGI_FORMAT_BC4_UNORM:
            case DXGI_FORMAT_BC4_SNORM:
            case DXGI_FORMAT_BC5_TYPELESS:
            case DXGI_FORMAT_BC5_UNORM:
            case DXGI_FORMAT_BC5_SNORM:
            case DXGI_FORMAT_BC6H_TYPELESS:
            case DXGI_FORMAT_BC6H_UF16:
            case DXGI_FORMAT_BC6H_SF16:
            case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            case DXGI_FORMAT_B8G8R8X8_UNORM:
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            case DXGI_FORMAT_B5G6R5_UNORM:
            case DXGI_FORMAT_R11G11B10_FLOAT:
            case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
            case DXGI_FORMAT_R8_UNORM:
            case DXGI_FORMAT_R8G8_UNORM:
            case DXGI_FORMAT_R16_FLOAT:
            case DXGI_FORMAT_R16_UNORM:
            case DXGI_FORMAT_R16G16_FLOAT:
            case DXGI_FORMAT_R16G16_UNORM:
            case DXGI_FORMAT_R32_FLOAT:
            case DXGI_FORMAT_R32G32_FLOAT:
            case DXGI_FORMAT_R32G32B32_FLOAT:
                return false;
            default:
                return true;
            }
        }

        /// @brief 頂点アニメーションで頂点が元の位置から動きうる距離の目安 [m]
        /// @details VertexAnimation.hlsli の式の各項の振幅の上限を、風の強さ 2（VertexAnimationParams の上限）・
        ///          マテリアルの強さ 1 で見積もる（カリング用の箱を広げる量なので大きめでよい）。
        ///          植物・海草: 全体の曲げ ≦ 1.25 S A、しなり ≦ (1.2 + 0.6 S) S B、震え ≦ S R
        ///          魚        : 体の横揺れ ≦ 振幅、胸びれ ≦ 1.12 × 振幅
        float EstimateVertexAnimationReach(VertexAnimationType type, const Vector4& anim)
        {
            constexpr float kStrength = 2.0f;
            switch (type) {
            case VertexAnimationType::Plant:
            case VertexAnimationType::Seagrass:
                return 1.25f * kStrength * std::abs(anim.w)
                    + (1.2f + 0.6f * kStrength) * kStrength * std::abs(anim.z)
                    + kStrength * std::abs(anim.x);
            case VertexAnimationType::Fish:
                return std::abs(anim.y) + 1.12f * std::abs(anim.z);
            default:
                return 0.0f;
            }
        }

        /// @brief 箱を各方向へ reach だけ広げる（無効な箱はそのまま）
        void ExpandBounds(BoundingBox& bounds, float reach)
        {
            if (reach <= 0.0f || !bounds.IsValid()) {
                return;
            }
            bounds.min = bounds.min - Vector3{ reach, reach, reach };
            bounds.max = bounds.max + Vector3{ reach, reach, reach };
        }
    }

    void ModelResource::CreateGeometryBuffers(size_t lod0IndexCount)
    {
        // indexCount_ は原型（LOD0）のインデックス数（BLAS の構築はモデル全体 = LOD0 を描く。LOD はサブメッシュの範囲で描く）
        // 頂点数・インデックス数を設定
        vertexCount_ = static_cast<UINT>(modelData_.vertices.size());
        indexCount_ = static_cast<UINT>(lod0IndexCount);

        // ===== 頂点／インデックスバッファを VRAM（DEFAULT ヒープ）へ配置する =====
        // UPLOAD ヒープはシステムメモリ常駐のため、毎フレームの頂点フェッチが PCIe を
        // 経由し、CPU・DWM・他プロセスと帯域を奪い合って GBuffer の時間が不安定になる。
        // テクスチャと同じく中間バッファ経由でコピーし、以後は VRAM から読ませる。
        const size_t vertexBytes = sizeof(VertexData) * modelData_.vertices.size();
        const size_t indexBytes = sizeof(uint32_t) * modelData_.indices.size();
        ID3D12Device* device = dxCommon_->GetDevice();

        vertexBuffer_.Reset(
            ResourceFactory::CreateBufferResource(device, vertexBytes, D3D12_HEAP_TYPE_DEFAULT),
            D3D12_RESOURCE_STATE_GENERIC_READ);
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexUploadBuffer =
            ResourceFactory::CreateBufferResource(device, vertexBytes);

        vertexBufferView_.BufferLocation = vertexBuffer_.GpuAddress();
        vertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBytes);
        vertexBufferView_.StrideInBytes = sizeof(VertexData);

        void* mapped = nullptr;
        vertexUploadBuffer->Map(0, nullptr, &mapped);
        memcpy(mapped, modelData_.vertices.data(), vertexBytes);
        vertexUploadBuffer->Unmap(0, nullptr);

        indexBuffer_.Reset(
            ResourceFactory::CreateBufferResource(device, indexBytes, D3D12_HEAP_TYPE_DEFAULT),
            D3D12_RESOURCE_STATE_GENERIC_READ);
        Microsoft::WRL::ComPtr<ID3D12Resource> indexUploadBuffer =
            ResourceFactory::CreateBufferResource(device, indexBytes);

        indexBufferView_.BufferLocation = indexBuffer_.GpuAddress();
        indexBufferView_.SizeInBytes = static_cast<UINT>(indexBytes);
        indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

        void* mappedIndex = nullptr;
        indexUploadBuffer->Map(0, nullptr, &mappedIndex);
        memcpy(mappedIndex, modelData_.indices.data(), indexBytes);
        indexUploadBuffer->Unmap(0, nullptr);

        // 頂点の色は頂点と同じ数だけあるモデルだけ、別の配列（StructuredBuffer<uint>）で持つ
        const bool hasColors = !modelData_.vertexColors.empty()
            && modelData_.vertexColors.size() == modelData_.vertices.size();
        const size_t colorBytes = hasColors ? sizeof(uint32_t) * modelData_.vertexColors.size() : 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> colorUploadBuffer;
        vertexColorBuffer_ = GpuResource();
        vertexColorSRV_ = UniqueDescriptor();
        if (hasColors) {
            vertexColorBuffer_.Reset(
                ResourceFactory::CreateBufferResource(device, colorBytes, D3D12_HEAP_TYPE_DEFAULT),
                D3D12_RESOURCE_STATE_GENERIC_READ);
            colorUploadBuffer = ResourceFactory::CreateBufferResource(device, colorBytes);
            void* mappedColor = nullptr;
            colorUploadBuffer->Map(0, nullptr, &mappedColor);
            memcpy(mappedColor, modelData_.vertexColors.data(), colorBytes);
            colorUploadBuffer->Unmap(0, nullptr);

            if (DescriptorAllocator* descriptors = dxCommon_->GetDescriptorAllocator()) {
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = DXGI_FORMAT_UNKNOWN;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Buffer.FirstElement = 0;
                srvDesc.Buffer.NumElements = static_cast<UINT>(modelData_.vertexColors.size());
                srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
                srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
                vertexColorSRV_ = UniqueDescriptor(*descriptors,
                    descriptors->CreateSRV(vertexColorBuffer_.Get(), srvDesc, "ModelVertexColorSRV"));
            }
        }

        // GENERIC_READ で作られた DEFAULT ヒープバッファを COPY_DEST へ落としてコピーし、
        // 読み側（IA / DXR BLAS 構築 / スキニング SRV）が使える GENERIC_READ へ戻す。
        // 積み先はフレームの描画用リストではなく専用のアップロードコンテキスト。
        {
            UploadContext::ScopedRecording recording = dxCommon_->GetUploadContext()->BeginRecording();
            assert(recording.IsValid() && "ModelResource: failed to begin an upload recording");
            ID3D12GraphicsCommandList* cmdList = recording.List();

            {
                BarrierBatch batch(cmdList);
                batch.Transition(vertexBuffer_, D3D12_RESOURCE_STATE_COPY_DEST);
                batch.Transition(indexBuffer_, D3D12_RESOURCE_STATE_COPY_DEST);
                if (hasColors) {
                    batch.Transition(vertexColorBuffer_, D3D12_RESOURCE_STATE_COPY_DEST);
                }
            }

            cmdList->CopyBufferRegion(vertexBuffer_.Get(), 0, vertexUploadBuffer.Get(), 0, vertexBytes);
            cmdList->CopyBufferRegion(indexBuffer_.Get(), 0, indexUploadBuffer.Get(), 0, indexBytes);
            if (hasColors) {
                cmdList->CopyBufferRegion(vertexColorBuffer_.Get(), 0, colorUploadBuffer.Get(), 0, colorBytes);
            }

            {
                BarrierBatch batch(cmdList);
                batch.Transition(vertexBuffer_, D3D12_RESOURCE_STATE_GENERIC_READ);
                batch.Transition(indexBuffer_, D3D12_RESOURCE_STATE_GENERIC_READ);
                if (hasColors) {
                    batch.Transition(vertexColorBuffer_, D3D12_RESOURCE_STATE_GENERIC_READ);
                }
            }

            // 中間バッファはコピー完了まで生きていればよい。メンバで抱え続けない。
            recording.KeepAlive(std::move(vertexUploadBuffer));
            recording.KeepAlive(std::move(indexUploadBuffer));
            if (hasColors) {
                recording.KeepAlive(std::move(colorUploadBuffer));
            }
        } // ここで Close → ExecuteCommandLists → Signal

        // ===== ローカル空間AABBを頂点データから算出 =====
        localBoundingBox_ = BoundingBox(); // 無効値で初期化
        for (const auto& vertex : modelData_.vertices) {
            if (vertex.position.x < localBoundingBox_.min.x) localBoundingBox_.min.x = vertex.position.x;
            if (vertex.position.y < localBoundingBox_.min.y) localBoundingBox_.min.y = vertex.position.y;
            if (vertex.position.z < localBoundingBox_.min.z) localBoundingBox_.min.z = vertex.position.z;
            if (vertex.position.x > localBoundingBox_.max.x) localBoundingBox_.max.x = vertex.position.x;
            if (vertex.position.y > localBoundingBox_.max.y) localBoundingBox_.max.y = vertex.position.y;
            if (vertex.position.z > localBoundingBox_.max.z) localBoundingBox_.max.z = vertex.position.z;
        }

        // ===== サブメッシュ単位のローカルAABBを算出（Hi-Zオクルージョンカリングの判定単位） =====
        // LOD0 のインデックス範囲が参照する頂点のみ集計する。簡略化 LOD（lods[1..]）は
        // 同一頂点バッファの部分集合を参照するため、LOD0 の AABB で保守的に覆える。
        subMeshLocalBounds_.assign(modelData_.subMeshes.size(), BoundingBox());
        float modelAnimationReach = 0.0f;
        for (size_t s = 0; s < modelData_.subMeshes.size(); ++s) {
            const SubMeshData& subMesh = modelData_.subMeshes[s];
            BoundingBox& bounds = subMeshLocalBounds_[s];
            // 頂点アニメーションの種類はマテリアルの既定値（glTF の extras）で決まる
            const VertexAnimationType animationType = (subMesh.materialIndex < modelData_.materials.size())
                ? modelData_.materials[subMesh.materialIndex].vertexAnimation
                : VertexAnimationType::None;
            float animationReach = 0.0f;
            const size_t endIndex = (std::min)(
                static_cast<size_t>(subMesh.startIndex) + subMesh.indexCount,
                modelData_.indices.size());
            for (size_t i = subMesh.startIndex; i < endIndex; ++i) {
                const int32_t vertexIndex = modelData_.indices[i];
                if (vertexIndex < 0
                    || static_cast<size_t>(vertexIndex) >= modelData_.vertices.size()) {
                    continue;
                }
                const auto& p = modelData_.vertices[vertexIndex].position;
                if (p.x < bounds.min.x) bounds.min.x = p.x;
                if (p.y < bounds.min.y) bounds.min.y = p.y;
                if (p.z < bounds.min.z) bounds.min.z = p.z;
                if (p.x > bounds.max.x) bounds.max.x = p.x;
                if (p.y > bounds.max.y) bounds.max.y = p.y;
                if (p.z > bounds.max.z) bounds.max.z = p.z;
                if (animationType != VertexAnimationType::None) {
                    animationReach = (std::max)(animationReach,
                        EstimateVertexAnimationReach(animationType, modelData_.vertices[vertexIndex].animData));
                }
            }
            // 揺れ・泳ぎで箱からはみ出した葉先やひれが、視錐台・Hi-Z の判定で欠けないように広げる
            ExpandBounds(bounds, animationReach);
            modelAnimationReach = (std::max)(modelAnimationReach, animationReach);
        }
        ExpandBounds(localBoundingBox_, modelAnimationReach);

        // 頂点アニメーション用の値を持つか（持たなければ種類を指定しても形は変わらない）
        hasVertexAnimationData_ = std::any_of(modelData_.vertices.begin(), modelData_.vertices.end(),
            [](const VertexData& v) {
                return v.animData.x != 0.0f || v.animData.y != 0.0f || v.animData.z != 0.0f || v.animData.w != 0.0f;
            });

        // 不変条件の検証: 添字対応の配列は必ずサブメッシュ数と一致させる。
        // AABB が無効なサブメッシュ（空範囲・全インデックス不正）は描画には害がない
        // （利用側が IsValid で除外しカリング対象外になるだけ）が、データ異常なので記録する
        assert(subMeshLocalBounds_.size() == modelData_.subMeshes.size());
        for (size_t s = 0; s < subMeshLocalBounds_.size(); ++s) {
            if (!subMeshLocalBounds_[s].IsValid()) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource, "{}",
                    "[ModelResource] サブメッシュAABBが無効（空範囲/不正インデックス）: "
                    + filePath_ + " subMesh=" + std::to_string(s));
            }
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ModelResource::GetVertexColorSRV() const
    {
        return vertexColorSRV_.IsValid() ? vertexColorSRV_.Gpu() : fallbackVertexColorSRV_;
    }

    const BoundingBox& ModelResource::GetSubMeshLocalBounds(uint32_t subMeshIndex) const
    {
        if (subMeshIndex < subMeshLocalBounds_.size()) {
            return subMeshLocalBounds_[subMeshIndex];
        }
        // ここに来るのはサブメッシュとの対応ズレ（呼び出し側のバグ）か未ロード時のみ。
        // Debug では即停止し、Release では保守的にモデル全体 AABB で誤カリングを防ぐ
        assert(false && "GetSubMeshLocalBounds: subMeshIndex out of range (index mismatch bug)");
        if (!subMeshBoundsRangeWarned_) {
            subMeshBoundsRangeWarned_ = true;
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource, "{}",
                "[ModelResource] GetSubMeshLocalBounds 範囲外アクセス（モデル全体AABBへフォールバック）: "
                + filePath_ + " subMesh=" + std::to_string(subMeshIndex)
                + " size=" + std::to_string(subMeshLocalBounds_.size()));
        }
        return localBoundingBox_;
    }

    void ModelResource::LoadFromFile(const std::string& directoryPath, const std::string& filename)
    {
        assert(dxCommon_ && textureManager_);

        // モデルの読み込み（初回は変換も）は重いモデルで数百 ms かかる。起動中はローディング画面を刻む
        if (StartupProgress::IsActive()) {
            StartupProgress::Tick(filename.c_str());
        }

        // 派生データの .cmesh（LOD の生成と GPU 向けの並べ替えを済ませた物）を読む
        const std::filesystem::path sourcePath = Logger::GetInstance().Utf8ToPath(directoryPath + "/" + filename);
        MeshAsset asset;
        if (!ModelDerivedData::Load(sourcePath, asset) || !asset.hasMesh) {
            throw std::runtime_error(std::format("モデルを読めません: {}/{}", directoryPath, filename));
        }
        const size_t lod0IndexCount = asset.lod0IndexCount;
        modelData_ = std::move(asset.model);
        fileAnimations_ = std::move(asset.animations);

        // RootNodeを保存
        rootNode_ = modelData_.rootNode;

        // Skeletonを作成
        skeleton_ = SkeletonLoader::CreateSkeleton(modelData_.rootNode);

        CreateGeometryBuffers(lod0IndexCount);

        // ===== マテリアルのPBRテクスチャを並列読み込み =====
        // 色空間: ベースカラー/エミッシブは sRGB、法線/MR/AO はデータテクスチャなので Linear。
        materialTextureHandles_.resize(modelData_.materials.size());

        // 全マテリアルで必要なテクスチャリクエストを収集する。
        std::vector<TextureManager::LoadRequest> textureRequests;
        textureRequests.push_back({ "white1x1.png", TextureColorSpace::SRGB });

        for (size_t i = 0; i < modelData_.materials.size(); ++i) {
            const MaterialAsset& material = modelData_.materials[i];
            if (!material.baseColorTexture.empty())
                textureRequests.push_back({ material.baseColorTexture, TextureColorSpace::SRGB });
            if (!material.metallicRoughnessTexture.empty())
                textureRequests.push_back({ material.metallicRoughnessTexture, TextureColorSpace::Linear });
            if (!material.normalTexture.empty())
                textureRequests.push_back({ material.normalTexture, TextureColorSpace::Normal });
            if (!material.occlusionTexture.empty())
                textureRequests.push_back({ material.occlusionTexture, TextureColorSpace::Linear });
            if (!material.emissiveTexture.empty())
                textureRequests.push_back({ material.emissiveTexture, TextureColorSpace::SRGB });
        }

        // 収集したリクエストを一括で並列ロードに投入し、完了を待つ。
        textureManager_->Load(textureRequests);

        // 全テクスチャがキャッシュ済みの状態で割り当てる（キャッシュヒットのみ）。
        // 白1x1は値が(1,1,1,1)のため sRGB/Linear どちらのフォールバックとしても正しい。
        D3D12_GPU_DESCRIPTOR_HANDLE defaultWhiteTexture = textureManager_->Load("white1x1.png").gpuHandle;

        for (size_t i = 0; i < modelData_.materials.size(); ++i) {
            const MaterialAsset& material = modelData_.materials[i];
            PBRTextureHandles& handles = materialTextureHandles_[i];

            // BaseColor（Albedo）テクスチャ
            if (!material.baseColorTexture.empty()) {
                const TextureManager::LoadedTexture baseColor = textureManager_->Load(material.baseColorTexture);
                handles.baseColor = baseColor.gpuHandle;
                handles.baseColorMayHaveAlpha =
                    !baseColor.texture || FormatMayHaveAlpha(baseColor.texture->GetDesc().Format);
            } else {
                handles.baseColor = defaultWhiteTexture;
                handles.baseColorMayHaveAlpha = false;
            }

            // MetallicRoughnessテクスチャ (G=Roughness, B=Metallic)
            if (!material.metallicRoughnessTexture.empty()) {
                handles.metallicRoughness = textureManager_->Load(material.metallicRoughnessTexture, TextureColorSpace::Linear).gpuHandle;
                handles.hasMetallicRoughness = true;
            } else {
                handles.metallicRoughness = defaultWhiteTexture;
                handles.hasMetallicRoughness = false;
            }

            // Normalマップ
            if (!material.normalTexture.empty()) {
                handles.normal = textureManager_->Load(material.normalTexture, TextureColorSpace::Normal).gpuHandle;
                handles.hasNormal = true;
            } else {
                handles.normal = defaultWhiteTexture;
                handles.hasNormal = false;
            }

            // Occlusionマップ
            if (!material.occlusionTexture.empty()) {
                handles.occlusion = textureManager_->Load(material.occlusionTexture, TextureColorSpace::Linear).gpuHandle;
                handles.hasOcclusion = true;
            } else {
                handles.occlusion = defaultWhiteTexture;
                handles.hasOcclusion = false;
            }

            // Emissiveマップ
            if (!material.emissiveTexture.empty()) {
                handles.emissive = textureManager_->Load(material.emissiveTexture).gpuHandle;
                handles.hasEmissive = true;
            } else {
                handles.emissive = defaultWhiteTexture;
                handles.hasEmissive = false;
            }
        }

        CreateDefaultMaterials();

        // ファイルパスを保存（デバッグ用）
        filePath_ = directoryPath + "/" + filename;
        isLoaded_ = true;
    }

    void ModelResource::LoadFromModelData(ModelData&& data, const std::string& name, bool generateLods)
    {
        assert(dxCommon_ && textureManager_);

        modelData_ = std::move(data);

        // RootNodeを保存
        rootNode_ = modelData_.rootNode;

        // 原型のインデックス数を控えてから LOD を足し（作らない形は除く）、GPU 向けに並べ替える
        const size_t lod0IndexCount = modelData_.indices.size();
        if (generateLods) {
            MeshImporter::GenerateLods(modelData_);
        } else {
            MeshImporter::InitLod0(modelData_);
        }
        MeshImporter::OptimizeForGpu(modelData_);
        CreateGeometryBuffers(lod0IndexCount);

        // マテリアルテクスチャハンドルの設定（デフォルト白テクスチャ）
        D3D12_GPU_DESCRIPTOR_HANDLE defaultWhiteTexture = textureManager_->Load("white1x1.png").gpuHandle;
        materialTextureHandles_.resize(std::max<size_t>(modelData_.materials.size(), 1));
        for (auto& handles : materialTextureHandles_) {
            handles.baseColor = defaultWhiteTexture;
            handles.metallicRoughness = defaultWhiteTexture;
            handles.normal = defaultWhiteTexture;
            handles.occlusion = defaultWhiteTexture;
            handles.emissive = defaultWhiteTexture;
        }

        CreateDefaultMaterials();

        filePath_ = name.empty() ? "<procedural>" : name;
        isLoaded_ = true;
    }

    void ModelResource::CreateDefaultMaterials()
    {
        // Model::Initialize が以前スロットごとに複製していたロジックをリソース側へ集約。
        // 全 Model インスタンスがオーバーライドするまでこの共有インスタンスを参照するため、
        // 同一モデルを複数配置してもマテリアルCBVアドレスが一致しインスタンシングバッチが統合される。
        const size_t materialCount = (std::max<size_t>)(modelData_.materials.size(), 1);
        defaultMaterials_.clear();
        defaultMaterials_.reserve(materialCount);
        for (size_t i = 0; i < materialCount; ++i) {
            auto instance = std::make_unique<MaterialInstance>();
            instance->Initialize(*dxCommon_);

            if (i < modelData_.materials.size()) {
                const MaterialAsset& asset = modelData_.materials[i];
                instance->SetColor(asset.baseColorFactor);
                instance->SetMetallic(asset.metallicFactor);
                instance->SetRoughness(asset.roughnessFactor);
                instance->SetEmissiveFactor(asset.emissiveFactor);
                instance->SetAlphaCutoff(asset.alphaCutoff);
                // アルファで抜く材質はディザではなくアルファカットオフで判定する
                if (asset.alphaMask) {
                    instance->SetDitheringEnabled(false);
                }
                // 頂点アニメーション（glTF の extras。植物・海草・魚のモデルは置くだけで動く）
                instance->SetVertexAnimation(asset.vertexAnimation);
                instance->SetVertexAnimStrength(asset.vertexAnimStrength);
                instance->SetVertexAnimSpeed(asset.vertexAnimSpeed);
            }

            // 法線マップのみフラグ制御（法線はファクター乗算で無効化できないため）
            instance->SetNormalMapEnabled(materialTextureHandles_[i].hasNormal);

            defaultMaterials_.push_back(std::move(instance));
        }
    }

    const MaterialInstance* ModelResource::GetDefaultMaterial(uint32_t materialIndex) const
    {
        if (defaultMaterials_.empty()) {
            return nullptr;
        }
        const size_t index = (materialIndex < defaultMaterials_.size()) ? materialIndex : 0;
        return defaultMaterials_[index].get();
    }

    const Animation* ModelResource::GetAnimation(const std::string& name) const {
        if (animations_.empty()) {
            return nullptr;
        }

        // 名前が空文字列の場合は最初のアニメーションを返す
        if (name.empty()) {
            return &animations_.begin()->second;
        }

        // 指定された名前のアニメーションを検索
        auto it = animations_.find(name);
        if (it != animations_.end()) {
            return &it->second;
        }

        return nullptr;
    }

    void ModelResource::AddAnimation(const std::string& name, const Animation& animation) {
        animations_[name] = animation;
    }

    const ModelResource::PBRTextureHandles& ModelResource::GetMaterialTextures(uint32_t materialIndex) const {
        assert(materialIndex < materialTextureHandles_.size());
        return materialTextureHandles_[materialIndex];
    }
}
