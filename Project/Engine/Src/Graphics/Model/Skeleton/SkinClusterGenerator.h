#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "Graphics/Model/Skeleton/SkinCluster.h"
#include "Graphics/Model/ModelData.h"
#include "Skeleton.h"

// 前方宣言
namespace CoreEngine {
    class DirectXCommon;
    class DescriptorManager;
}

/// @brief スキンクラスターを生成するクラス

namespace CoreEngine
{
class SkinClusterGenerator {
public:
    /// @brief スキンクラスターを生成
    /// @param device デバイス
    /// @param skeleton スケルトン
    /// @param modelData モデルデータ
    /// @param descriptorManager ディスクリプタマネージャー
    /// @param sourceVertexBuffer GPUスキニング（CS）が読み取る元頂点バッファ（ModelResourceが保持するもの）
    /// @param vertexCount 頂点数（sourceVertexBufferの要素数）
    /// @return 生成されたスキンクラスター
    static CoreEngine::SkinCluster CreateSkinCluster(
        const Microsoft::WRL::ComPtr<ID3D12Device>& device,
        const Skeleton& skeleton,
        const ModelData& modelData,
        CoreEngine::DescriptorManager* descriptorManager,
        ID3D12Resource* sourceVertexBuffer,
        UINT vertexCount);
    
    /// @brief スキンクラスターを更新
    /// @param skinCluster 更新するスキンクラスター
    /// @param skeleton スケルトン
    static void Update(SkinCluster& skinCluster, const Skeleton& skeleton);
};
}
