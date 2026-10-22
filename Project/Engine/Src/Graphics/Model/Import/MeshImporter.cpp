#include "pch.h"
#include "MeshImporter.h"
#include "Graphics/Model/Animation/AnimationLoader.h"
#include "Graphics/Model/Import/CmeshFile.h"
#include "Graphics/Model/ModelLoader.h"
#include "Graphics/Model/VertexData.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include "externals/meshoptimizer/src/meshoptimizer.h"

#include <algorithm>
#include <format>
#include <limits>

namespace CoreEngine
{
    namespace
    {
        // LOD を作るのはハイポリのモデルだけ（小さいモデルは簡略化の効果が無く、生成とバッファの増加だけが残る）
        constexpr size_t kMinTriangleCountForLod = 10000;   // モデル全体でこれ未満なら LOD を作らない
        constexpr size_t kMinSubMeshIndexCount = 3 * 256;   // サブメッシュ単位の下限（小物は原型のまま）
        constexpr float kLodRatios[] = { 0.25f, 0.06f };    // LOD1 / LOD2 の目標三角形比率
        constexpr float kLodTargetError = 0.05f;            // 許容誤差（AABB 対角に対する相対値）
        // Sloppy への切り替えは目標三角形数への到達を優先するので、誤差の上限を実質無効にする
        constexpr float kLodSloppyTargetError = std::numeric_limits<float>::max();
        constexpr float kOverdrawThreshold = 1.05f;         // ACMR を最大 5% 悪化させてよい範囲でオーバードローを減らす

        /// @brief テクスチャのパスを綴り（Application/… か Engine/…）にする（根の外ならそのまま）
        std::string ToSpelled(const std::string& path)
        {
            if (path.empty()) {
                return path;
            }
            const std::filesystem::path relative = ProjectPaths::MakeRelative(Logger::GetInstance().Utf8ToPath(path));
            if (relative.empty()) {
                return path;
            }
            std::string spelled = Logger::GetInstance().PathToUtf8(relative);
            std::replace(spelled.begin(), spelled.end(), '\\', '/');
            return spelled;
        }
    }

    std::string MeshImporter::Settings()
    {
        return std::format("lod={},{};err={};min={};sub={};overdraw={};stride={};cmesh={}",
            kLodRatios[0], kLodRatios[1], kLodTargetError, kMinTriangleCountForLod, kMinSubMeshIndexCount,
            kOverdrawThreshold, sizeof(VertexData), CmeshFile::kVersion);
    }

    bool MeshImporter::Import(const std::filesystem::path& sourcePath, const std::filesystem::path& cmeshPath)
    {
        Logger& log = Logger::GetInstance();
        MeshAsset asset;
        uint32_t meshCount = 0;
        if (!AnimationLoader::LoadAllAnimations(sourcePath, asset.animations, meshCount)) {
            log.Logf(LogLevel::WARNING, LogCategory::Resource, "モデルのファイルを読めません: {}", log.PathToUtf8(sourcePath));
            return false;
        }

        if (meshCount > 0) {
            asset.model = ModelLoader::LoadModelFile(
                log.PathToUtf8(sourcePath.parent_path()), log.PathToUtf8(sourcePath.filename()));
            asset.lod0IndexCount = static_cast<uint32_t>(asset.model.indices.size());
            GenerateLods(asset.model);
            OptimizeForGpu(asset.model);
            for (MaterialAsset& material : asset.model.materials) {
                material.baseColorTexture = ToSpelled(material.baseColorTexture);
                material.metallicRoughnessTexture = ToSpelled(material.metallicRoughnessTexture);
                material.normalTexture = ToSpelled(material.normalTexture);
                material.occlusionTexture = ToSpelled(material.occlusionTexture);
                material.emissiveTexture = ToSpelled(material.emissiveTexture);
            }
            asset.hasMesh = !asset.model.vertices.empty() && !asset.model.indices.empty();
        }

        if (!asset.hasMesh && asset.animations.empty()) {
            log.Logf(LogLevel::WARNING, LogCategory::Resource, "メッシュもアニメーションも無いので .cmesh にできません: {}",
                log.PathToUtf8(sourcePath));
            return false;
        }
        return CmeshFile::Write(cmeshPath, asset);
    }

    void MeshImporter::InitLod0(ModelData& data)
    {
        for (auto& subMesh : data.subMeshes) {
            subMesh.lods[0] = { subMesh.startIndex, subMesh.indexCount };
            subMesh.lodCount = 1;
        }
    }

    void MeshImporter::GenerateLods(ModelData& data)
    {
        // lods[0] は常に原型の範囲を指すよう先に埋めておく
        InitLod0(data);

        const size_t totalTriangles = data.indices.size() / 3;
        if (totalTriangles < kMinTriangleCountForLod) {
            return;
        }

        const size_t vertexCount = data.vertices.size();
        const float* vertexPositions = &data.vertices[0].position.x;

        // 簡略化結果は既存インデックス列の末尾へ追記する（頂点バッファは共有）
        std::vector<int32_t>& indices = data.indices;

        for (auto& subMesh : data.subMeshes) {
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

                // 目標に届かない（つながっていない三角形の集まりなど）か、孤立部品を全部消した（count==0）ときは
                // クラスタリング型の Sloppy 版に切り替える
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

                // 削減が 1 割未満なら段を足さない
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
                std::format("MeshImporter: SubMesh \"{}\" LOD生成 LOD0={} tris, LOD段数={}{}{}",
                    subMesh.name,
                    subMesh.indexCount / 3,
                    subMesh.lodCount,
                    subMesh.lodCount > 1 ? std::format(", LOD1={} tris", subMesh.lods[1].indexCount / 3) : "",
                    subMesh.lodCount > 2 ? std::format(", LOD2={} tris", subMesh.lods[2].indexCount / 3) : ""));
        }
    }

    void MeshImporter::OptimizeForGpu(ModelData& data)
    {
        const size_t vertexCount = data.vertices.size();
        if (vertexCount == 0 || data.indices.empty()) {
            return;
        }

        // indices は int32_t、meshopt は unsigned int を取る（頂点の番号は非負なので同じ幅で読み替える）
        unsigned int* indexData = reinterpret_cast<unsigned int*>(data.indices.data());
        const float* vertexPositions = &data.vertices[0].position.x;
        const size_t stride = sizeof(VertexData);

        // 各 LOD の範囲へ、頂点キャッシュ最適化 → オーバードロー最適化（どちらも頂点は動かさない）
        std::vector<unsigned int> scratch;
        for (const auto& subMesh : data.subMeshes) {
            for (uint32_t lod = 0; lod < subMesh.lodCount; ++lod) {
                const SubMeshLodRange& range = subMesh.lods[lod];
                if (range.indexCount < 3) {
                    continue;
                }
                unsigned int* rangeIndices = indexData + range.startIndex;
                scratch.assign(rangeIndices, rangeIndices + range.indexCount);
                meshopt_optimizeVertexCache(
                    rangeIndices, scratch.data(), range.indexCount, vertexCount);

                scratch.assign(rangeIndices, rangeIndices + range.indexCount);
                meshopt_optimizeOverdraw(
                    rangeIndices, scratch.data(), range.indexCount,
                    vertexPositions, vertexCount, stride, kOverdrawThreshold);
            }
        }

        // 静的モデルだけ、全 LOD・全サブメッシュのインデックスから作った並べ替えで頂点を並べ替える
        if (data.skinClusterData.empty()) {
            std::vector<unsigned int> remap(vertexCount);
            meshopt_optimizeVertexFetchRemap(
                remap.data(), indexData, data.indices.size(), vertexCount);

            // remapVertexBuffer は書き込み先がばらばらなので、別のバッファへ書いてから差し替える
            std::vector<VertexData> remappedVertices(vertexCount);
            meshopt_remapVertexBuffer(
                remappedVertices.data(), data.vertices.data(),
                vertexCount, stride, remap.data());
            data.vertices = std::move(remappedVertices);

            meshopt_remapIndexBuffer(
                indexData, indexData, data.indices.size(), remap.data());
        }
    }
}
