#pragma once
#include <vector>
#include <span>
#include <array>
#include <d3d12.h>
#include <wrl.h>

#include "Engine/Math/Matrix/Matrix4x4.h"

///  頂点に影響を与えるジョイントの最大数

namespace CoreEngine
{
const uint32_t kNumMaxInfluence = 4;

/// @brief 頂点ごとのインフルエンス情報
/// 頂点がどのジョイント（最大4つ）からどの程度の影響を受けるかを表す
struct VertexInfluence {
    std::array<float, kNumMaxInfluence> weights;      // 各ジョイントからの影響度
    std::array<int32_t, kNumMaxInfluence> jointIndices; // 影響を与えるジョイントのインデックス
};

/// @brief MatrixPaletteの各要素（Well）に格納する行列
/// スケルトン空間での変換行列を保持
struct WellForGPU {
    Matrix4x4 skeletonSpaceMatrix;              // スケルトン空間行列（位置用）
    Matrix4x4 skeletonSpaceInverseTransposeMatrix; // スケルトン空間逆転置行列（法線用）
};

/// @brief スキンクラスター
/// CPUで作られた諸々のデータをGPUで扱えるようにするための構造体
struct SkinCluster {
    std::vector<Matrix4x4> inverseBindPoseMatrices;   // BindPoseの逆行列（Joint数分）

    Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource;  // Influence用リソース
    D3D12_VERTEX_BUFFER_VIEW influenceBufferView;              // InfluenceのBufferView
    std::span<VertexInfluence> mappedInfluence;                // Influenceデータをマップしたもの

    Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource;    // Palette用リソース
    std::span<WellForGPU> mappedPalette;                       // Paletteデータをマップしたもの
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> paletteSrvHandle; // PaletteのSRV
};
}
