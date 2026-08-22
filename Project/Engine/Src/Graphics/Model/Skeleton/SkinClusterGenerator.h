#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "Graphics/Model/Skeleton/SkinCluster.h"
#include "Graphics/Model/ModelData.h"
#include "Skeleton.h"

// 前方宣言
namespace CoreEngine {
    class GraphicsCore;
    class DescriptorAllocator;
}

namespace CoreEngine
{
/// @brief スキンクラスターを生成するクラス
class SkinClusterGenerator {
public:
    /// @brief スキンクラスターを生成
    /// @param sourceVertexBuffer GPU スキニング（CS）が読み取る元頂点バッファ
    static CoreEngine::SkinCluster CreateSkinCluster(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        const Skeleton& skeleton,
        const ModelData& modelData,
        CoreEngine::DescriptorAllocator* descriptorAllocator,
        ID3D12Resource* sourceVertexBuffer,
        UINT vertexCount);
    
    /// @brief スキンクラスターを更新
    /// @param skinCluster 更新するスキンクラスター
    /// @param skeleton スケルトン
    static void Update(SkinCluster& skinCluster, const Skeleton& skeleton);
};
}
