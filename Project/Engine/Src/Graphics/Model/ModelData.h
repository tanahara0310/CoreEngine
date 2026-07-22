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

/// @brief LOD 1段分のインデックス範囲
struct SubMeshLodRange {
    uint32_t startIndex = 0;    // インデックスバッファ内の開始位置
    uint32_t indexCount = 0;    // インデックス数
};

/// @brief サブメッシュ情報（メッシュごとのマテリアルとインデックス範囲）
struct SubMeshData {
    static constexpr uint32_t kMaxLodCount = 3; // LOD0（原型）+ 簡略化2段

    std::string name;           // サブメッシュ名
    uint32_t startIndex;        // インデックスバッファ内の開始位置（LOD0）
    uint32_t indexCount;        // このサブメッシュのインデックス数（LOD0）
    uint32_t materialIndex;     // 使用するマテリアルのインデックス

    // LOD情報。lods[0] は常に LOD0（= startIndex / indexCount と同じ範囲）。
    // 簡略化インデックスは同じ頂点バッファを参照するため VB の追加は不要。
    SubMeshLodRange lods[kMaxLodCount] = {};
    uint32_t lodCount = 1;

    /// @brief LODレベルをクランプして範囲を返す（バイアス適用後の安全な取得口）
    const SubMeshLodRange& GetLod(uint32_t lodIndex) const {
        const uint32_t clamped = (lodIndex < lodCount) ? lodIndex : (lodCount - 1);
        return lods[clamped];
    }
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
