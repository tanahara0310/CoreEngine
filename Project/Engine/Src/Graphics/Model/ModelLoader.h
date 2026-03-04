#pragma once

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <string>
#include <vector>

#include "Graphics/Model/ModelData.h"
#include "Graphics/Model/MaterialAsset.h"
#include "Graphics/Model/Node.h"
#include "Graphics/Model/VertexData.h"
#include "Math/Matrix/Matrix4x4.h"

/// @brief モデルファイル読み込みクラス

namespace CoreEngine
{
    class ModelLoader {
    public:
        /// @brief モデルファイルを読み込む
        /// @param directoryPath ディレクトリパス
        /// @param filename ファイル名
        /// @return 読み込んだモデルデータ
        static ModelData LoadModelFile(const std::string& directoryPath, const std::string& filename);

    private:
        // ===== ファイル読み込み・検証 =====
        
        /// @brief Assimpでファイルを読み込む
        /// @param filepath ファイルパス
        /// @return Assimpシーン
        static const aiScene* LoadAssimpFile(const std::string& filepath);
        
        /// @brief シーンの妥当性を検証
        /// @param scene Assimpシーン
        /// @param filepath ファイルパス
        static void ValidateScene(const aiScene* scene, const std::string& filepath);

        // ===== フェーズ別処理 =====
        
        /// @brief マテリアルデータを読み込む
        /// @param scene Assimpシーン
        /// @param directoryPath ディレクトリパス
        /// @return マテリアルアセット配列
        static std::vector<MaterialAsset> LoadMaterials(const aiScene* scene, const std::string& directoryPath);
        
        /// @brief メッシュデータを読み込む
        /// @param scene Assimpシーン
        /// @param outResult 出力先ModelData
        static void LoadMeshData(const aiScene* scene, ModelData& outResult);
        
        /// @brief スキンクラスター情報を読み込む
        /// @param mesh Assimpメッシュ
        /// @param baseVertexIndex ベース頂点インデックス
        /// @param outResult 出力先ModelData
        static void LoadSkinClusterData(const aiMesh* mesh, uint32_t baseVertexIndex, ModelData& outResult);

        // ===== ヘルパー関数 =====
        
        /// @brief 頂点データをVertexDataに変換
        /// @param mesh Assimpメッシュ
        /// @param vertexIndex 頂点インデックス
        /// @return 変換された頂点データ
        static VertexData ConvertVertex(const aiMesh* mesh, uint32_t vertexIndex);
        
        /// @brief マテリアルからテクスチャパスを抽出
        /// @param material Assimpマテリアル
        /// @param type テクスチャタイプ
        /// @param index テクスチャインデックス
        /// @param directoryPath ディレクトリパス
        /// @return テクスチャパス（存在しない場合は空文字列）
        static std::string ExtractTexturePath(
            const aiMaterial* material,
            aiTextureType type,
            uint32_t index,
            const std::string& directoryPath);
        
        /// @brief バインドポーズ行列を計算
        /// @param offsetMatrix Assimpオフセット行列
        /// @return バインドポーズ行列
        static Matrix4x4 CalculateBindPoseMatrix(const aiMatrix4x4& offsetMatrix);

        /// @brief Nodeを再帰的に読み込む
        /// @param node AssimpのNode
        /// @return 変換されたNode
        static Node ReadNode(aiNode* node);
        
        // ===== ログ出力ヘルパー =====
        
        /// @brief ロード開始ログを出力
        /// @param filename ファイル名
        /// @param directoryPath ディレクトリパス
        static void LogLoadStart(const std::string& filename, const std::string& directoryPath);
        
        /// @brief ロード完了ログを出力
        /// @param result ロード結果
        static void LogLoadComplete(const ModelData& result);
    };
}
