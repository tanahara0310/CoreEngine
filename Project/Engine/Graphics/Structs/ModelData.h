#pragma once
#include <vector>
#include <map>

#include "MaterialData.h"
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

/// @brief モデルデータを表す構造体
struct ModelData {
	std::map<std::string, JointWeightData> skinClusterData; // スキンクラスター（ジョイントと頂点のウェイト情報）
	std::vector<VertexData> vertices; // 頂点データ
	std::vector<int32_t> indices;   // インデックスデータ
	MaterialData material;        // マテリアルデータ
	Node rootNode;             // Node階層構造のルート
};
}
