#pragma once

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <string>

#include "Engine/Graphics/Structs/ModelData.h"
#include "Engine/Graphics/Structs/Node.h"
#include "Engine/Math/Matrix/Matrix4x4.h"

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
    /// @brief Assimpでファイルを読み込む
    /// @param filepath ファイルパス
    /// @return Assimpシーン
    static const aiScene* LoadAssimpFile(const std::string& filepath);

    /// @brief Nodeを再帰的に読み込む
    /// @param node AssimpのNode
    /// @return 変換されたNode
    static Node ReadNode(aiNode* node);

};
}
