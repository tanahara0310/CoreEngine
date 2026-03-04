#include "ModelLoader.h"

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
        LogLoadStart(filename, directoryPath);
        const aiScene* scene = LoadAssimpFile(fullPath);
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

    const aiScene* ModelLoader::LoadAssimpFile(const std::string& filepath)
    {
        // 静的Importerを2つ用意（通常用とスキニング用）
        static Assimp::Importer importerNormal;
        static Assimp::Importer importerSkinned;

        Logger::GetInstance().Log(std::format("Loading model file: {}", filepath), LogLevel::INFO, LogCategory::Graphics);

        // まず軽量なフラグでシーンを読み込み、スキニングデータがあるか確認
        Assimp::Importer checkImporter;
        const aiScene* checkScene = checkImporter.ReadFile(
            filepath.c_str(),
            aiProcess_Triangulate  // 最小限のフラグで読み込み
        );

        if (!checkScene) {
            std::string errorMsg = std::format("Failed to load model file: {}\nAssimp Error: {}\nPlease check if the file exists and the path is correct.",
                filepath, checkImporter.GetErrorString());
            Logger::GetInstance().Log(errorMsg, LogLevel::Error, LogCategory::Graphics);
            FileErrorDialog::ShowModelError("Failed to load model file", filepath, checkImporter.GetErrorString());
            assert(false && errorMsg.c_str());
            return nullptr;
        }

        // スキニングデータがあるかチェック
        bool hasSkinning = false;
        for (uint32_t i = 0; i < checkScene->mNumMeshes; ++i) {
            if (checkScene->mMeshes[i]->HasBones()) {
                hasSkinning = true;
                break;
            }
        }

        // スキニングデータの有無に応じてフラグを決定
        const aiScene* scene = nullptr;
        if (hasSkinning) {
            // スキニングモデル: aiProcess_PreTransformVerticesを使用しない
            Logger::GetInstance().Log(std::format("Detected skinning data, loading without PreTransformVertices: {}", filepath), LogLevel::INFO, LogCategory::Graphics);
            scene = importerSkinned.ReadFile(
                filepath.c_str(),
                aiProcess_Triangulate |
                aiProcess_GenSmoothNormals |
                aiProcess_CalcTangentSpace |
                aiProcess_LimitBoneWeights |      // ボーンウェイトを4つに制限
                aiProcess_ConvertToLeftHanded |
                aiProcess_FlipUVs
            );
        } else {
            // 通常モデル: aiProcess_PreTransformVerticesを使用
            Logger::GetInstance().Log(std::format("No skinning data, loading with PreTransformVertices: {}", filepath), LogLevel::INFO, LogCategory::Graphics);
            scene = importerNormal.ReadFile(
                filepath.c_str(),
                aiProcess_Triangulate |
                aiProcess_GenSmoothNormals |
                aiProcess_CalcTangentSpace |
                aiProcess_PreTransformVertices |  // Node変換を頂点に適用（スキニングなしの場合のみ）
                aiProcess_ConvertToLeftHanded |
                aiProcess_FlipUVs
            );
        }

        if (!scene) {
            const char* errorStr = hasSkinning ? importerSkinned.GetErrorString() : importerNormal.GetErrorString();
            std::string errorMsg = std::format("Failed to load model file: {}\nAssimp Error: {}\nPlease check if the file exists and the path is correct.",
                filepath, errorStr);
            Logger::GetInstance().Log(errorMsg, LogLevel::Error, LogCategory::Graphics);
            FileErrorDialog::ShowModelError("Failed to load model file", filepath, errorStr);
            assert(false && errorMsg.c_str());
            return nullptr;
        }

        Logger::GetInstance().Log(std::format("Model loaded successfully: {}", filepath), LogLevel::INFO, LogCategory::Graphics);
        return scene;
    }

    void ModelLoader::ValidateScene(const aiScene* scene, const std::string& filepath)
    {
        if (!scene || !scene->HasMeshes()) {
            std::string errorMsg = std::format("Failed to load model or model has no meshes: {}", filepath);
            Logger::GetInstance().Log(errorMsg, LogLevel::Error, LogCategory::Graphics);
            assert(false && errorMsg.c_str());
        }
    }

    // ===== フェーズ別処理 =====

    std::vector<MaterialAsset> ModelLoader::LoadMaterials(const aiScene* scene, const std::string& directoryPath)
    {
        Logger::GetInstance().Log(std::format("Loading {} materials...", scene->mNumMaterials), LogLevel::INFO, LogCategory::Graphics);

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

            Logger::GetInstance().Log(std::format("  Material[{}]: {}", matIndex, material.name), LogLevel::INFO, LogCategory::Graphics);

            // 各種テクスチャパスを取得
            material.baseColorTexture = ExtractTexturePath(aiMat, aiTextureType_DIFFUSE, 0, directoryPath);
            material.metallicRoughnessTexture = ExtractTexturePath(aiMat, aiTextureType_UNKNOWN, 0, directoryPath);
            material.normalTexture = ExtractTexturePath(aiMat, aiTextureType_NORMALS, 0, directoryPath);
            material.occlusionTexture = ExtractTexturePath(aiMat, aiTextureType_LIGHTMAP, 0, directoryPath);
            material.emissiveTexture = ExtractTexturePath(aiMat, aiTextureType_EMISSIVE, 0, directoryPath);

            // ログ出力
            if (!material.baseColorTexture.empty())
                Logger::GetInstance().Log(std::format("    - BaseColor: {}", material.baseColorTexture), LogLevel::INFO, LogCategory::Graphics);
            if (!material.metallicRoughnessTexture.empty())
                Logger::GetInstance().Log(std::format("    - MetallicRoughness: {}", material.metallicRoughnessTexture), LogLevel::INFO, LogCategory::Graphics);
            if (!material.normalTexture.empty())
                Logger::GetInstance().Log(std::format("    - Normal: {}", material.normalTexture), LogLevel::INFO, LogCategory::Graphics);
            if (!material.occlusionTexture.empty())
                Logger::GetInstance().Log(std::format("    - Occlusion: {}", material.occlusionTexture), LogLevel::INFO, LogCategory::Graphics);
            if (!material.emissiveTexture.empty())
                Logger::GetInstance().Log(std::format("    - Emissive: {}", material.emissiveTexture), LogLevel::INFO, LogCategory::Graphics);
        }

        return materials;
    }

    void ModelLoader::LoadMeshData(const aiScene* scene, ModelData& outResult)
    {
        Logger::GetInstance().Log(std::format("Loading {} meshes...", scene->mNumMeshes), LogLevel::INFO, LogCategory::Graphics);

        for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
            aiMesh* mesh = scene->mMeshes[meshIndex];
            assert(mesh->HasNormals() && "Mesh must have normals");
            assert(mesh->HasTextureCoords(0) && "Mesh must have texture coordinates");

            // サブメッシュ情報を記録
            SubMeshData subMesh;
            subMesh.name = mesh->mName.C_Str();
            subMesh.startIndex = static_cast<uint32_t>(outResult.indices.size());
            subMesh.materialIndex = mesh->mMaterialIndex;

            Logger::GetInstance().Log(std::format("  Mesh[{}]: \"{}\"", meshIndex, subMesh.name), LogLevel::INFO, LogCategory::Graphics);
            Logger::GetInstance().Log(std::format("    - Vertices: {}", mesh->mNumVertices), LogLevel::INFO, LogCategory::Graphics);
            Logger::GetInstance().Log(std::format("    - Faces: {}", mesh->mNumFaces), LogLevel::INFO, LogCategory::Graphics);
            Logger::GetInstance().Log(std::format("    - MaterialIndex: {}", subMesh.materialIndex), LogLevel::INFO, LogCategory::Graphics);

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
            Logger::GetInstance().Log(std::format("    - IndexRange: [{}, {})", subMesh.startIndex, subMesh.startIndex + subMesh.indexCount), LogLevel::INFO, LogCategory::Graphics);

            outResult.subMeshes.push_back(subMesh);

            // スキンクラスター情報の読み込み
            LoadSkinClusterData(mesh, baseVertexIndex, outResult);
        }
    }

    void ModelLoader::LoadSkinClusterData(const aiMesh* mesh, uint32_t /*baseVertexIndex*/, ModelData& outResult)
    {
        for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
            aiBone* bone = mesh->mBones[boneIndex];
            std::string jointName = bone->mName.C_Str();
            JointWeightData& jointWeightData = outResult.skinClusterData[jointName];

            // バインドポーズ行列を計算
            jointWeightData.inverseBindPoseMatrix = CalculateBindPoseMatrix(bone->mOffsetMatrix);

            // 頂点ウェイト情報を格納
            for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                jointWeightData.vertexWeights.push_back({
                    bone->mWeights[weightIndex].mWeight,
                    bone->mWeights[weightIndex].mVertexId
                    });
            }
        }
    }

    // ===== ヘルパー関数 =====

    VertexData ModelLoader::ConvertVertex(const aiMesh* mesh, uint32_t vertexIndex)
    {
        aiVector3D position = mesh->mVertices[vertexIndex];
        aiVector3D normal = mesh->mNormals[vertexIndex];
        aiVector3D texCoord = mesh->mTextureCoords[0][vertexIndex];

        VertexData vertex{};
        vertex.position = { position.x, position.y, position.z, 1.0f };
        vertex.normal = { normal.x, normal.y, normal.z };
        vertex.texcoord = { texCoord.x, texCoord.y };

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
        if (material->GetTexture(type, index, &texPath) == AI_SUCCESS) {
            return directoryPath + "/" + texPath.C_Str();
        }
        return "";
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
        Logger::GetInstance().Log("============================================", LogLevel::INFO, LogCategory::Graphics);
        Logger::GetInstance().Log(std::format("Loading model: {} from directory: {}", filename, directoryPath), LogLevel::INFO, LogCategory::Graphics);
    }

    void ModelLoader::LogLoadComplete(const ModelData& result)
    {
        Logger::GetInstance().Log(
            std::format("Model loading completed: Total {} vertices, {} indices, {} materials, {} submeshes",
                result.vertices.size(), result.indices.size(), result.materials.size(), result.subMeshes.size()),
            LogLevel::INFO, LogCategory::Graphics);
        Logger::GetInstance().Log("============================================", LogLevel::INFO, LogCategory::Graphics);
    }
}
