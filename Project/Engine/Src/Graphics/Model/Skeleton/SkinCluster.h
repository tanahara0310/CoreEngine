#pragma once
#include <vector>
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include <span>
#include <array>
#include <d3d12.h>
#include <wrl.h>

#include "Graphics/Shader/CBufferLayout.h"
#include "Math/Matrix/Matrix4x4.h"

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

// 頂点バッファ / StructuredBuffer 要素なので詰め込み規則で検証する
static constexpr Cb::Field kVertexInfluenceFields[] = {
    CB_FIELD(VertexInfluence, weights), CB_FIELD(VertexInfluence, jointIndices),
};
CB_VERIFY_STRIDE(VertexInfluence, kVertexInfluenceFields);

/// @brief MatrixPaletteの各要素（Well）に格納する行列
/// スケルトン空間での変換行列を保持
struct WellForGPU {
    Matrix4x4 skeletonSpaceMatrix;              // スケルトン空間行列（位置用）
    Matrix4x4 skeletonSpaceInverseTransposeMatrix; // スケルトン空間逆転置行列（法線用）
};

static constexpr Cb::Field kWellForGPUFields[] = {
    CB_FIELD(WellForGPU, skeletonSpaceMatrix), CB_FIELD(WellForGPU, skeletonSpaceInverseTransposeMatrix),
};
CB_VERIFY_STRIDE(WellForGPU, kWellForGPUFields);

/// @brief スキンクラスター
/// CPUで作られた諸々のデータをGPUで扱えるようにするための構造体
struct SkinCluster {
    std::vector<Matrix4x4> inverseBindPoseMatrices;   // BindPoseの逆行列（Joint数分）

    Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource;  // Influence用リソース
    D3D12_VERTEX_BUFFER_VIEW influenceBufferView;              // InfluenceのBufferView
    std::span<VertexInfluence> mappedInfluence;                // Influenceデータをマップしたもの
    DescriptorHandle influenceSrvHandle; // InfluenceのSRV（CS読み取り用）

    Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource;    // Palette用リソース
    std::span<WellForGPU> mappedPalette;                       // Paletteデータをマップしたもの
    DescriptorHandle paletteSrvHandle; // PaletteのSRV

    // ===== GPUスキニング（ComputeShader）関連 =====

    DescriptorHandle sourceVertexSrvHandle; // 元頂点バッファのSRV（CS読み取り用）

    Microsoft::WRL::ComPtr<ID3D12Resource> outputVertexResource; // CSが書き込むスキニング後頂点バッファ（UAV）
    D3D12_VERTEX_BUFFER_VIEW outputVertexBufferView;             // 上記をそのまま描画時の頂点バッファとして使う
    DescriptorHandle outputUavHandle; // 出力バッファのUAV
    D3D12_RESOURCE_STATES outputBufferState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; // 出力バッファの現在のリソース状態

    Microsoft::WRL::ComPtr<ID3D12Resource> skinningParamsCB; // SkinningParams（頂点数）用定数バッファ

    // 今フレームまだGPUスキニングを実行していないか（true = 描画前にCS Dispatchが必要）
    // SkinClusterGenerator::Update() でtrueにセットされ、Model側でDispatch後にfalseへ戻す。
    bool needsGPUSkinning = true;
};
}
