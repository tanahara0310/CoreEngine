#include "pch.h"
#include "ModelLoader.h"

#include <assimp/GltfMaterial.h> // AI_MATKEY_GLTF_ALPHACUTOFF

#include <cassert>
#include <format>
#include "Graphics/Model/VertexData.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"
#include "Utility/FileErrorDialog/FileErrorDialog.h"


namespace CoreEngine
{
    // ===== メイン関数 =====

    ModelData ModelLoader::LoadModelFile(const std::string& directoryPath, const std::string& filename)
    {
        std::string fullPath = directoryPath + "/" + filename;

        // ===== フェーズ1: ファイルロードと検証 =====
        // Importer はこの関数のスコープで所有する（=呼び出しごとにローカル）。
        // static にすると PreloadModels の並列ロードでデータ競合するため注意。
        LogLoadStart(filename, directoryPath);
        Assimp::Importer importer;
        const aiScene* scene = LoadAssimpFile(importer, fullPath);
        ValidateScene(scene, fullPath);

        ModelData result;

        // ===== フェーズ2: マテリアル読み込み =====
        result.materials = LoadMaterials(scene, directoryPath);

        // ===== フェーズ3: メッシュデータ読み込み =====
        LoadMeshData(scene, result);

        // ===== フェーズ4: ノード階層読み込み =====
        result.rootNode = ReadNode(scene->mRootNode);

        // ===== フェーズ5: 完了ログ =====
        LogLoadComplete(result);

        return result;
    }

    // ===== ファイル読み込み・検証 =====

    const aiScene* ModelLoader::LoadAssimpFile(Assimp::Importer& importer, const std::string& filepath)
    {
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("Loading model file: {}", filepath));

        // Assimp の DefaultIOSystem は Windows でパスを UTF-8 とみなすので、
        // UTF-8 で統一してある文字列をそのまま渡す。
        // スキニング有無に依存しない共通フラグで 1 回だけフルパースする。
        const aiScene* scene = importer.ReadFile(
            filepath.c_str(),
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices | // 重複頂点の統合（インデックス化）。理由は上記コメント参照
            aiProcess_GenSmoothNormals |
            aiProcess_LimitBoneWeights |      // ボーンウェイトを4つに制限
            aiProcess_ConvertToLeftHanded |
            aiProcess_FlipUVs
        );

        if (!scene) {
            std::string errorMsg = std::format("Failed to load model file: {}\nAssimp Error: {}\nPlease check if the file exists and the path is correct.",
                filepath, importer.GetErrorString());
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource, "{}", errorMsg);
            FileErrorDialog::ShowModelError("Failed to load model file", filepath, importer.GetErrorString());
            assert(false && errorMsg.c_str());
            return nullptr;
        }

        // スキニングデータがあるかチェック
        bool hasSkinning = false;
        for (uint32_t i = 0; i < scene->mNumMeshes; ++i) {
            if (scene->mMeshes[i]->HasBones()) {
                hasSkinning = true;
                break;
            }
        }

        if (hasSkinning) {
            // スキニングモデル: Node変換を頂点へ焼き込むとボーン階層と矛盾するため何もしない
            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("Detected skinning data, keeping node hierarchy: {}", filepath));
        } else {
            // 通常モデル: 既存パース結果に対しPreTransformVerticesのみ追加適用（再パース不要）
            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("No skinning data, applying PreTransformVertices: {}", filepath));
            scene = importer.ApplyPostProcessing(aiProcess_PreTransformVertices);

            if (!scene) {
                std::string errorMsg = std::format("Failed to apply PreTransformVertices: {}\nAssimp Error: {}",
                    filepath, importer.GetErrorString());
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource, "{}", errorMsg);
                FileErrorDialog::ShowModelError("Failed to load model file", filepath, importer.GetErrorString());
                assert(false && errorMsg.c_str());
                return nullptr;
            }
        }

        // タンジェント計算は頂点統合（JoinIdenticalVertices）より後でなければならないため、
        // 独立したパスとして最後に適用する。理由は ReadFile 呼び出し前のコメント参照。
        // UV が異なる頂点は Join で統合されずに残るので、UV シームのタンジェント分割は保たれる。
        scene = importer.ApplyPostProcessing(aiProcess_CalcTangentSpace);

        if (!scene) {
            std::string errorMsg = std::format("Failed to apply CalcTangentSpace: {}\nAssimp Error: {}",
                filepath, importer.GetErrorString());
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource, "{}", errorMsg);
            FileErrorDialog::ShowModelError("Failed to load model file", filepath, importer.GetErrorString());
            assert(false && errorMsg.c_str());
            return nullptr;
        }

        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("Model loaded successfully: {}", filepath));
        return scene;
    }

    void ModelLoader::ValidateScene(const aiScene* scene, const std::string& filepath)
    {
        if (!scene || !scene->HasMeshes()) {
            std::string errorMsg = std::format("Failed to load model or model has no meshes: {}", filepath);
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource, "{}", errorMsg);
            assert(false && errorMsg.c_str());
        }
    }

    // ===== フェーズ別処理 =====

    std::vector<MaterialAsset> ModelLoader::LoadMaterials(const aiScene* scene, const std::string& directoryPath)
    {
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("Loading {} materials...", scene->mNumMaterials));

        std::vector<MaterialAsset> materials(scene->mNumMaterials);

        for (uint32_t matIndex = 0; matIndex < scene->mNumMaterials; ++matIndex) {
            aiMaterial* aiMat = scene->mMaterials[matIndex];
            MaterialAsset& material = materials[matIndex];

            // マテリアル名を取得
            aiString matName;
            if (aiMat->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS) {
                material.name = matName.C_Str();
            } else {
                material.name = std::format("Material_{}", matIndex);
            }

            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("  Material[{}]: {}", matIndex, material.name));

            // 各種テクスチャパスを取得
            // ベースカラーは glTF PBR の BASE_COLOR を優先し、レガシー形式は DIFFUSE にフォールバック
            material.baseColorTexture = ExtractTexturePath(aiMat, aiTextureType_BASE_COLOR, 0, directoryPath);
            if (material.baseColorTexture.empty()) {
                material.baseColorTexture = ExtractTexturePath(aiMat, aiTextureType_DIFFUSE, 0, directoryPath);
            }
            // MetallicRoughness は glTF では UNKNOWN として公開される。METALNESS にもフォールバック
            material.metallicRoughnessTexture = ExtractTexturePath(aiMat, aiTextureType_UNKNOWN, 0, directoryPath);
            if (material.metallicRoughnessTexture.empty()) {
                material.metallicRoughnessTexture = ExtractTexturePath(aiMat, aiTextureType_METALNESS, 0, directoryPath);
            }
            material.normalTexture = ExtractTexturePath(aiMat, aiTextureType_NORMALS, 0, directoryPath);
            material.occlusionTexture = ExtractTexturePath(aiMat, aiTextureType_LIGHTMAP, 0, directoryPath);
            material.emissiveTexture = ExtractTexturePath(aiMat, aiTextureType_EMISSIVE, 0, directoryPath);

            // ===== PBR ファクター（glTF はファクター×テクスチャの乗算合成） =====
            // キーが存在しない形式（OBJ 等）は MaterialAsset のデフォルト値を維持する。
            aiColor4D baseColor{};
            if (aiMat->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS) {
                material.baseColorFactor = { baseColor.r, baseColor.g, baseColor.b, baseColor.a };
            } else if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == AI_SUCCESS) {
                material.baseColorFactor = { baseColor.r, baseColor.g, baseColor.b, baseColor.a };
            }

            float metallicFactor = 0.0f;
            if (aiMat->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor) == AI_SUCCESS) {
                material.metallicFactor = metallicFactor;
            }
            float roughnessFactor = 0.0f;
            if (aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor) == AI_SUCCESS) {
                material.roughnessFactor = roughnessFactor;
            }

            aiColor3D emissiveColor{};
            if (aiMat->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == AI_SUCCESS) {
                material.emissiveFactor = { emissiveColor.r, emissiveColor.g, emissiveColor.b };
            }

            float alphaCutoff = 0.0f;
            if (aiMat->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff) == AI_SUCCESS) {
                material.alphaCutoff = alphaCutoff;
            }

            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format(
                "    - Factors: baseColor=({:.2f},{:.2f},{:.2f},{:.2f}) metallic={:.2f} roughness={:.2f} emissive=({:.2f},{:.2f},{:.2f})",
                material.baseColorFactor.x, material.baseColorFactor.y, material.baseColorFactor.z, material.baseColorFactor.w,
                material.metallicFactor, material.roughnessFactor,
                material.emissiveFactor.x, material.emissiveFactor.y, material.emissiveFactor.z));

            // ログ出力
            if (!material.baseColorTexture.empty())
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("    - BaseColor: {}", material.baseColorTexture));
            if (!material.metallicRoughnessTexture.empty())
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("    - MetallicRoughness: {}", material.metallicRoughnessTexture));
            if (!material.normalTexture.empty())
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("    - Normal: {}", material.normalTexture));
            if (!material.occlusionTexture.empty())
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("    - Occlusion: {}", material.occlusionTexture));
            if (!material.emissiveTexture.empty())
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("    - Emissive: {}", material.emissiveTexture));
        }

        return materials;
    }

    void ModelLoader::LoadMeshData(const aiScene* scene, ModelData& outResult)
    {
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("Loading {} meshes...", scene->mNumMeshes));

        for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
            aiMesh* mesh = scene->mMeshes[meshIndex];
            assert(mesh->HasNormals() && "Mesh must have normals");
            // UV は必須ではない（持たない場合は ConvertVertex が (0,0) を入れる）

            // サブメッシュ情報を記録
            SubMeshData subMesh;
            subMesh.name = mesh->mName.C_Str();
            subMesh.startIndex = static_cast<uint32_t>(outResult.indices.size());
            subMesh.materialIndex = mesh->mMaterialIndex;

            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("  Mesh[{}]: \"{}\"", meshIndex, subMesh.name));
            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("    - Vertices: {}", mesh->mNumVertices));
            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("    - Faces: {}", mesh->mNumFaces));
            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("    - MaterialIndex: {}", subMesh.materialIndex));

            // 頂点データの変換
            uint32_t baseVertexIndex = static_cast<uint32_t>(outResult.vertices.size());
            for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
                outResult.vertices.push_back(ConvertVertex(mesh, vertexIndex));
            }

            // インデックスデータの取得
            for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
                const aiFace& face = mesh->mFaces[faceIndex];
                assert(face.mNumIndices == 3 && "Only triangle faces are supported");

                for (uint32_t element = 0; element < face.mNumIndices; ++element) {
                    uint32_t vertexIndex = face.mIndices[element];
                    outResult.indices.push_back(baseVertexIndex + vertexIndex);
                }
            }

            // サブメッシュのインデックス数を計算
            subMesh.indexCount = static_cast<uint32_t>(outResult.indices.size()) - subMesh.startIndex;
            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("    - IndexRange: [{}, {})", subMesh.startIndex, subMesh.startIndex + subMesh.indexCount));

            outResult.subMeshes.push_back(subMesh);

            // スキンクラスター情報の読み込み
            LoadSkinClusterData(mesh, baseVertexIndex, outResult);
        }
    }

    void ModelLoader::LoadSkinClusterData(const aiMesh* mesh, uint32_t baseVertexIndex, ModelData& outResult)
    {
        for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
            aiBone* bone = mesh->mBones[boneIndex];
            std::string jointName = bone->mName.C_Str();
            JointWeightData& jointWeightData = outResult.skinClusterData[jointName];

            // バインドポーズ行列を計算
            jointWeightData.inverseBindPoseMatrix = CalculateBindPoseMatrix(bone->mOffsetMatrix);

            // aiBone の mVertexId はメッシュ内ローカルのインデックス。
            // ModelData::vertices は全メッシュを 1 本に連結しているため、
            // 開始位置 baseVertexIndex を足してグローバルなインデックスへ直す
            // （マルチメッシュで別メッシュの頂点にウェイトが乗る不具合を防ぐ）。
            for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                jointWeightData.vertexWeights.push_back({
                    bone->mWeights[weightIndex].mWeight,
                    baseVertexIndex + bone->mWeights[weightIndex].mVertexId
                    });
            }
        }
    }

    // ===== ヘルパー関数 =====

    VertexData ModelLoader::ConvertVertex(const aiMesh* mesh, uint32_t vertexIndex)
    {
        aiVector3D position = mesh->mVertices[vertexIndex];
        aiVector3D normal = mesh->mNormals[vertexIndex];

        VertexData vertex{};
        vertex.position = { position.x, position.y, position.z, 1.0f };
        vertex.normal = { normal.x, normal.y, normal.z };

        // UV を持たないモデル（マテリアルの色だけで表現された glTF など）があるため、
        // mTextureCoords[0] は null になりうる。参照する前に必ず存在を確認する。
        if (mesh->HasTextureCoords(0)) {
            aiVector3D texCoord = mesh->mTextureCoords[0][vertexIndex];
            vertex.texcoord = { texCoord.x, texCoord.y };
        } else {
            vertex.texcoord = { 0.0f, 0.0f };
        }

        // Tangent（接線）をコピー（aiProcess_CalcTangentSpaceで計算済み）
        if (mesh->HasTangentsAndBitangents()) {
            aiVector3D tangent = mesh->mTangents[vertexIndex];
            vertex.tangent = { tangent.x, tangent.y, tangent.z };
        } else {
            // Tangentがない場合はデフォルト値（X軸方向）
            vertex.tangent = { 1.0f, 0.0f, 0.0f };
        }

        return vertex;
    }

    std::string ModelLoader::ExtractTexturePath(
        const aiMaterial* material,
        aiTextureType type,
        uint32_t index,
        const std::string& directoryPath)
    {
        aiString texPath;
        if (material->GetTexture(type, index, &texPath) != AI_SUCCESS) {
            return "";
        }

        std::string rawPath = texPath.C_Str();

        // バックスラッシュをスラッシュに統一
        std::replace(rawPath.begin(), rawPath.end(), '\\', '/');

        // 絶対パスの場合はそのまま返す
        if (rawPath.length() >= 2 && rawPath[1] == ':') {
            return rawPath;
        }

        // directoryPath と結合し、".." などを含む場合に lexically_normal() で正規化する。
        // directoryPath も Assimp が返す rawPath も UTF-8 なので、path との往復は
        // Logger の Utf8ToPath / PathToUtf8 を通す（ANSI 変換を挟むと非 ASCII が壊れる）。
        Logger& log = Logger::GetInstance();
        std::filesystem::path combined = log.Utf8ToPath(directoryPath) / log.Utf8ToPath(rawPath);
        return log.PathToUtf8(combined.lexically_normal());
    }

    Matrix4x4 ModelLoader::CalculateBindPoseMatrix(const aiMatrix4x4& offsetMatrix)
    {
        // Inverse()はconst版がないため、一時変数を作成
        aiMatrix4x4 offsetMatrixCopy = offsetMatrix;
        aiMatrix4x4 bindPoseMatrixAssimp = offsetMatrixCopy.Inverse();

        aiVector3D scale, translate;
        aiQuaternion rotate;
        bindPoseMatrixAssimp.Decompose(scale, rotate, translate);

        Matrix4x4 bindPoseMatrix = MathCore::Matrix::MakeAffine(
            { scale.x, scale.y, scale.z },
            { rotate.x, rotate.y, rotate.z, rotate.w },
            { translate.x, translate.y, translate.z }
        );

        return MathCore::Matrix::Inverse(bindPoseMatrix);
    }

    Node ModelLoader::ReadNode(aiNode* node)
    {
        Node result;

        // aiNodeからSRTを抽出
        aiVector3D scale, translate;
        aiQuaternion rotate;
        node->mTransformation.Decompose(scale, rotate, translate);

        // QuaternionTransformに変換（座標変換はaiProcess_ConvertToLeftHandedで処理済み）
        result.transform.scale = { scale.x, scale.y, scale.z };
        result.transform.rotate = { rotate.x, rotate.y, rotate.z, rotate.w };
        result.transform.translate = { translate.x, translate.y, translate.z };

        // localMatrixを再構築
        result.localMatrix = MathCore::Matrix::MakeAffine(
            result.transform.scale,
            result.transform.rotate,
            result.transform.translate
        );

        // Node名を格納
        result.name = node->mName.C_Str();

        // 子Nodeを再帰的に読み込み
        result.children.resize(node->mNumChildren);
        for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
            result.children[childIndex] = ReadNode(node->mChildren[childIndex]);
        }

        return result;
    }

    // ===== ログ出力ヘルパー =====

    void ModelLoader::LogLoadStart(const std::string& filename, const std::string& directoryPath)
    {
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", "============================================");
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("Loading model: {} from directory: {}", filename, directoryPath));
    }

    void ModelLoader::LogLoadComplete(const ModelData& result)
    {
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", 
            std::format("Model loading completed: Total {} vertices, {} indices, {} materials, {} submeshes",
                result.vertices.size(), result.indices.size(), result.materials.size(), result.subMeshes.size()));
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", "============================================");
    }
}


