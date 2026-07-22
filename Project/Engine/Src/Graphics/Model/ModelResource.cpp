#include "pch.h"
#include "ModelResource.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Model/ModelLoader.h"
#include "Graphics/Model/Skeleton/SkeletonLoader.h"
#include "Graphics/Model/VertexData.h"
#include "Utility/Logger/Logger.h"

#include "externals/meshoptimizer/src/meshoptimizer.h"

#include <cassert>
#include <format>
#include <limits>


namespace CoreEngine
{
    void ModelResource::Initialize(DirectXCommon* dxCommon, ResourceFactory* factory, TextureManager* textureMg)
    {
        dxCommon_ = dxCommon;
        resourceFactory_ = factory;
        textureManager_ = textureMg;
    }

    void ModelResource::GenerateSubMeshLods()
    {
        // LOD 生成の対象はハイポリの静的モデルのみ。
        // 小さいモデルは簡略化の効果が無く、生成コストとバッファ増加だけが残る。
        constexpr size_t kMinTriangleCountForLod = 10000;      // モデル全体でこれ未満なら LOD を作らない
        constexpr size_t kMinSubMeshIndexCount = 3 * 256;      // サブメッシュ単位の下限（小物は原型のまま）
        constexpr float kLodRatios[] = { 0.25f, 0.06f };       // LOD1 / LOD2 の目標三角形比率
        constexpr float kLodTargetError = 0.05f;               // 許容誤差（AABB対角に対する相対値）
        // Sloppy フォールバックは「目標三角形数への到達」を最優先するため誤差上限を実質無効化する。
        // （0.5 程度だと誤差上限が先に効いて 40k tris 級の葉サブメッシュが削減されず残る）
        constexpr float kLodSloppyTargetError = std::numeric_limits<float>::max();

        // lods[0] は常に原型の範囲を指すよう先に埋めておく
        for (auto& subMesh : modelData_.subMeshes) {
            subMesh.lods[0] = { subMesh.startIndex, subMesh.indexCount };
            subMesh.lodCount = 1;
        }

        const size_t totalTriangles = modelData_.indices.size() / 3;
        if (totalTriangles < kMinTriangleCountForLod) {
            return;
        }

        const size_t vertexCount = modelData_.vertices.size();
        const float* vertexPositions = &modelData_.vertices[0].position.x;

        // 簡略化結果は既存インデックス列の末尾へ追記する（頂点バッファは共有）
        std::vector<int32_t>& indices = modelData_.indices;

        for (auto& subMesh : modelData_.subMeshes) {
            if (subMesh.indexCount < kMinSubMeshIndexCount) {
                continue;
            }

            uint32_t previousCount = subMesh.indexCount;
            for (float ratio : kLodRatios) {
                const size_t targetIndexCount = static_cast<size_t>(subMesh.indexCount * ratio) / 3 * 3;
                if (targetIndexCount < 3) {
                    break;
                }

                std::vector<unsigned int> simplified(subMesh.indexCount);
                float resultError = 0.0f;
                // Prune オプションで小さな孤立部品（葉・小石など）の除去を許可する
                size_t simplifiedCount = meshopt_simplify(
                    simplified.data(),
                    reinterpret_cast<const unsigned int*>(indices.data()) + subMesh.startIndex,
                    subMesh.indexCount,
                    vertexPositions,
                    vertexCount,
                    sizeof(VertexData),
                    targetIndexCount,
                    kLodTargetError,
                    meshopt_SimplifyPrune,
                    &resultError);

                Logger::GetInstance().Logf(LogLevel::Trace, LogCategory::Resource, "{}",
                    std::format("LODデバッグ: SubMesh \"{}\" simplify結果 count={} target={} error={:.4f}",
                        subMesh.name, simplifiedCount, targetIndexCount, resultError));

                // トポロジー保存型で目標に届かない場合（葉・草などの非連結な三角形スープは
                // エッジコラプスできない）は、クラスタリング型の Sloppy 版へフォールバックする。
                // 見た目の劣化は大きめだが、遠景 LOD 用途では画面上ほぼ判別できない。
                // count==0 は Prune が孤立部品を丸ごと消して0になる過剰崩壊で、これも失敗扱いにする
                // （target*3/2 の上振れ判定だけだと 0 を素通りさせてしまい棄却止まりになる）。
                if (simplifiedCount == 0 || simplifiedCount > targetIndexCount * 3 / 2) {
                    simplifiedCount = meshopt_simplifySloppy(
                        simplified.data(),
                        reinterpret_cast<const unsigned int*>(indices.data()) + subMesh.startIndex,
                        subMesh.indexCount,
                        vertexPositions,
                        vertexCount,
                        sizeof(VertexData),
                        targetIndexCount,
                        kLodSloppyTargetError,
                        &resultError);

                    Logger::GetInstance().Logf(LogLevel::Trace, LogCategory::Resource, "{}",
                        std::format("LODデバッグ: SubMesh \"{}\" sloppyフォールバック count={} target={} error={:.4f}",
                            subMesh.name, simplifiedCount, targetIndexCount, resultError));
                }

                // 削減が 1 割未満なら段を追加する価値なし
                if (simplifiedCount == 0 || simplifiedCount >= static_cast<size_t>(previousCount) * 9 / 10) {
                    Logger::GetInstance().Logf(LogLevel::Trace, LogCategory::Resource, "{}",
                        std::format("LODデバッグ: SubMesh \"{}\" LOD段追加を棄却 count={} previousCount={} (閾値90%={})",
                            subMesh.name, simplifiedCount, previousCount, static_cast<size_t>(previousCount) * 9 / 10));
                    break;
                }

                SubMeshLodRange lodRange;
                lodRange.startIndex = static_cast<uint32_t>(indices.size());
                lodRange.indexCount = static_cast<uint32_t>(simplifiedCount);
                indices.insert(indices.end(),
                    reinterpret_cast<const int32_t*>(simplified.data()),
                    reinterpret_cast<const int32_t*>(simplified.data()) + simplifiedCount);

                subMesh.lods[subMesh.lodCount] = lodRange;
                ++subMesh.lodCount;
                previousCount = lodRange.indexCount;

                if (subMesh.lodCount >= SubMeshData::kMaxLodCount) {
                    break;
                }
            }

            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}",
                std::format("ModelResource: SubMesh \"{}\" LOD生成 LOD0={} tris, LOD段数={}{}{}",
                    subMesh.name,
                    subMesh.indexCount / 3,
                    subMesh.lodCount,
                    subMesh.lodCount > 1 ? std::format(", LOD1={} tris", subMesh.lods[1].indexCount / 3) : "",
                    subMesh.lodCount > 2 ? std::format(", LOD2={} tris", subMesh.lods[2].indexCount / 3) : ""));
        }
    }

    void ModelResource::OptimizeGeometryForGpu()
    {
        const size_t vertexCount = modelData_.vertices.size();
        if (vertexCount == 0 || modelData_.indices.empty()) {
            return;
        }

        // indices は int32_t、meshopt は unsigned int を取る。頂点インデックスは非負なので
        // 同幅の reinterpret_cast で問題ない（既存の LOD 生成コードと同じ規約）。
        unsigned int* indexData = reinterpret_cast<unsigned int*>(modelData_.indices.data());
        const float* vertexPositions = &modelData_.vertices[0].position.x;
        const size_t stride = sizeof(VertexData);
        constexpr float kOverdrawThreshold = 1.05f; // ACMR を最大 5% 悪化させてよい範囲でオーバードローを削減

        // (1)(2) 各 LOD 範囲へ 頂点キャッシュ最適化 → オーバードロー最適化（どちらも VB 不変）。
        // 範囲の startIndex/indexCount は変わらないため、後段のドロー範囲指定はそのまま有効。
        std::vector<unsigned int> scratch;
        for (const auto& subMesh : modelData_.subMeshes) {
            for (uint32_t lod = 0; lod < subMesh.lodCount; ++lod) {
                const SubMeshLodRange& range = subMesh.lods[lod];
                if (range.indexCount < 3) {
                    continue;
                }
                unsigned int* rangeIndices = indexData + range.startIndex;
                scratch.assign(rangeIndices, rangeIndices + range.indexCount);

                // 頂点キャッシュ最適化: post-transform キャッシュのヒット率を上げ VS 呼び出しを減らす
                meshopt_optimizeVertexCache(
                    rangeIndices, scratch.data(), range.indexCount, vertexCount);

                // オーバードロー最適化: カメラ非依存で前後関係の良い三角形順へ並べ替え
                scratch.assign(rangeIndices, rangeIndices + range.indexCount);
                meshopt_optimizeOverdraw(
                    rangeIndices, scratch.data(), range.indexCount,
                    vertexPositions, vertexCount, stride, kOverdrawThreshold);
            }
        }

        // (3) VertexFetch 最適化は VB を並べ替える。スキンモデルは influence バッファが
        // 頂点インデックス参照（SkinClusterGenerator）のため VB を動かせない。静的モデルのみ適用する。
        if (modelData_.skinClusterData.empty()) {
            // 全インデックス（全 LOD・全サブメッシュ）を対象に remap を作る。
            // これで LOD ごとの範囲を跨いで一貫した VB 並べ替えとインデックス再マップになる。
            std::vector<unsigned int> remap(vertexCount);
            const size_t uniqueVertices = meshopt_optimizeVertexFetchRemap(
                remap.data(), indexData, modelData_.indices.size(), vertexCount);
            (void)uniqueVertices;

            // remapVertexBuffer は scatter（vertices[i] → dst[remap[i]]）のため in-place 不可。
            // 別バッファへ書き出してから差し替える。remapIndexBuffer は要素毎の gather で in-place 可。
            std::vector<VertexData> remappedVertices(vertexCount);
            meshopt_remapVertexBuffer(
                remappedVertices.data(), modelData_.vertices.data(),
                vertexCount, stride, remap.data());
            modelData_.vertices = std::move(remappedVertices);

            meshopt_remapIndexBuffer(
                indexData, indexData, modelData_.indices.size(), remap.data());
        }
    }

    void ModelResource::CreateGeometryBuffers()
    {
        // LOD0（原型）のインデックス数を先に確定してから簡略化インデックスを末尾へ追記する。
        // indexCount_ は「原型のインデックス数」を維持する（BLAS 構築や DrawShadow が
        // モデル全体 = LOD0 を描く前提のため。LOD 描画はサブメッシュの範囲指定で行う）。
        const size_t lod0IndexCount = modelData_.indices.size();
        GenerateSubMeshLods();

        // GPU 向けジオメトリ再配置（VB/IB アップロード前・LOD 生成後）。
        // 頂点キャッシュ効率を上げ、頂点シェーダ呼び出し（GBuffer ラスタの頂点バウンド要因）を削減する。
        OptimizeGeometryForGpu();

        // 頂点数・インデックス数を設定
        vertexCount_ = static_cast<UINT>(modelData_.vertices.size());
        indexCount_ = static_cast<UINT>(lod0IndexCount);

        // 頂点バッファの作成
        vertexBuffer_ = ResourceFactory::CreateBufferResource(
            dxCommon_->GetDevice(),
            sizeof(VertexData) * modelData_.vertices.size());

        // 頂点バッファビューの設定
        vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
        vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelData_.vertices.size());
        vertexBufferView_.StrideInBytes = sizeof(VertexData);

        // 頂点データをGPUメモリにコピー
        void* mapped = nullptr;
        vertexBuffer_->Map(0, nullptr, &mapped);
        memcpy(mapped, modelData_.vertices.data(), sizeof(VertexData) * modelData_.vertices.size());
        vertexBuffer_->Unmap(0, nullptr);

        // インデックスバッファの作成
        indexBuffer_ = ResourceFactory::CreateBufferResource(
            dxCommon_->GetDevice(),
            sizeof(uint32_t) * modelData_.indices.size());

        // インデックスバッファビューの設定
        indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
        indexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * modelData_.indices.size());
        indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

        // インデックスデータをGPUメモリにコピー
        void* mappedIndex = nullptr;
        indexBuffer_->Map(0, nullptr, &mappedIndex);
        memcpy(mappedIndex, modelData_.indices.data(), sizeof(uint32_t) * modelData_.indices.size());
        indexBuffer_->Unmap(0, nullptr);

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
    }

    void ModelResource::LoadFromFile(const std::string& directoryPath, const std::string& filename)
    {
        assert(dxCommon_ && resourceFactory_ && textureManager_);

        // ModelLoaderを使用してモデルデータを読み込む
        ModelData modelData = ModelLoader::LoadModelFile(directoryPath, filename);

        // ModelDataを保存（スキンクラスター生成に必要）
        // moveセマンティクスを使って大きなデータを効率的に移動
        modelData_ = std::move(modelData);

        // RootNodeを保存
        rootNode_ = modelData_.rootNode;

        // Skeletonを作成
        skeleton_ = SkeletonLoader::CreateSkeleton(modelData_.rootNode);

        CreateGeometryBuffers();

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
                textureRequests.push_back({ material.normalTexture, TextureColorSpace::Linear });
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
                handles.baseColor = textureManager_->Load(material.baseColorTexture).gpuHandle;
            } else {
                handles.baseColor = defaultWhiteTexture;
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
                handles.normal = textureManager_->Load(material.normalTexture, TextureColorSpace::Linear).gpuHandle;
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

    void ModelResource::LoadFromModelData(ModelData&& data, const std::string& name)
    {
        assert(dxCommon_ && resourceFactory_ && textureManager_);

        modelData_ = std::move(data);

        // RootNodeを保存
        rootNode_ = modelData_.rootNode;

        CreateGeometryBuffers();

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
            instance->Initialize(dxCommon_->GetDevice());

            if (i < modelData_.materials.size()) {
                const MaterialAsset& asset = modelData_.materials[i];
                instance->SetColor(asset.baseColorFactor);
                instance->SetMetallic(asset.metallicFactor);
                instance->SetRoughness(asset.roughnessFactor);
                instance->SetEmissiveFactor(asset.emissiveFactor);
                instance->SetAlphaCutoff(asset.alphaCutoff);
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
