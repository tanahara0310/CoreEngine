#pragma once
#include <vector>
#include <map>

#include "MaterialAsset.h"
#include "VertexData.h"
#include "Node.h"


namespace CoreEngine
{
struct VertexWeightData {
    float weight;
    uint32_t vertexIndex;
};

struct JointWeightData {
    Matrix4x4 inverseBindPoseMatrix;
    std::vector<VertexWeightData> vertexWeights;
};

/// @brief サブメッシュ情報（メッシュごとのマテリアルとインデックス範囲）
struct SubMeshData {
    std::string name;           // サブメッシュ名
    uint32_t startIndex;        // インデックスバッファ内の開始位置
    uint32_t indexCount;        // このサブメッシュのインデックス数
    uint32_t materialIndex;     // 使用するマテリアルのインデックス
};

/// @brief モデルデータを表す構造体
struct ModelData {
    std::map<std::string, JointWeightData> skinClusterData; // スキンクラスター（ジョイントと頂点のウェイト情報）
    std::vector<VertexData> vertices;       // 頂点データ
    std::vector<int32_t> indices;           // インデックスデータ
    std::vector<MaterialAsset> materials;    // マテリアルデータ（複数対応）
	std::vector<SubMeshData> subMeshes;     // サブメッシュ情報
	Node rootNode;                          // Node階層構造のルート
};
}
