#pragma once

#include "Graphics/RHI/Resource/GpuResource.h"
#include "Graphics/RHI/Resource/PerFrameStructuredBuffer.h"
#include <vector>
#include "Graphics/RHI/Descriptor/UniqueDescriptor.h"
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
    UniqueDescriptor influenceSrvHandle; // InfluenceのSRV（CS読み取り用）

    PerFrameStructuredBuffer<WellForGPU> palette; // Palette（Joint数分。GPU へは SRV を引いたフレームの置き場へ写す）
    PerFrameStructuredBuffer<WellForGPU> prevPalette; // 1 フレーム前に描いた姿勢の Palette（前のフレームの位置を作る）

    std::vector<WellForGPU> lastPose;    // 最後に Update した姿勢の Palette
    uint64_t lastPoseFrame = UINT64_MAX; // 最後に Update したフレームの番号
    bool prevPoseStale = false;          // prevPalette が lastPose と違う（姿勢が止まったら 1 回だけ揃える）

    // ===== GPUスキニング（ComputeShader）関連 =====

    UniqueDescriptor sourceVertexSrvHandle; // 元頂点バッファのSRV（CS読み取り用）

    GpuResource outputVertexResource;                // CSが書き込むスキニング後頂点バッファ（UAV・ステート追跡込み）
    D3D12_VERTEX_BUFFER_VIEW outputVertexBufferView; // 上記をそのまま描画時の頂点バッファとして使う
    UniqueDescriptor outputUavHandle;                // 出力バッファのUAV
    UniqueDescriptor outputRawSrvHandle;             // 出力バッファの ByteAddressBuffer SRV（レイトレーシングのヒットシェーディングが読む）

    GpuResource prevPositionResource;       // CSが書く 1 フレーム前の姿勢の位置（float4・頂点の番号で引く）
    UniqueDescriptor prevPositionUavHandle; // 上記のUAV
    UniqueDescriptor prevPositionSrvHandle; // 上記の StructuredBuffer<float4> SRV（頂点シェーダがモーションベクターに使う）

    Microsoft::WRL::ComPtr<ID3D12Resource> skinningParamsCB; // SkinningParams（頂点数）用定数バッファ

    // 今フレームまだGPUスキニングを実行していないか（true = 描画前にCS Dispatchが必要）
    // SkinClusterGenerator::Update() でtrueにセットされ、Model側でDispatch後にfalseへ戻す。
    bool needsGPUSkinning = true;
};
}
