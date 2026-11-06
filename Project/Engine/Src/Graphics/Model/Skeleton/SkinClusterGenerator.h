#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "Graphics/Model/Skeleton/SkinCluster.h"
#include "Graphics/Model/ModelData.h"
#include "Skeleton.h"

// 前方宣言
namespace CoreEngine {
    class GraphicsCore;
}

namespace CoreEngine
{
/// @brief スキンクラスターを生成するクラス
class SkinClusterGenerator {
public:
    /// @brief スキンクラスターを生成
    /// @param graphics GPU のバッファと SRV を作る先
    /// @param sourceVertexBuffer GPU スキニング（CS）が読み取る元頂点バッファ
    static CoreEngine::SkinCluster CreateSkinCluster(
        GraphicsCore& graphics,
        const Skeleton& skeleton,
        const ModelData& modelData,
        ID3D12Resource* sourceVertexBuffer,
        UINT vertexCount);
    
    /// @brief スキンクラスターを更新
    /// @param skinCluster 更新するスキンクラスター
    /// @param skeleton スケルトン
    /// @details そのフレームで最初の更新なら、最後に更新した姿勢を前のフレームの姿勢にする
    static void Update(SkinCluster& skinCluster, const Skeleton& skeleton);

    /// @brief このフレームに更新が無く、前のフレームの姿勢が今の姿勢と違えば、前の姿勢を今の姿勢に揃える
    /// @return 揃えた（GPU スキニングを実行し直す）
    static bool SettlePrevPose(SkinCluster& skinCluster);
};
}
