#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "Engine/Graphics/Structs/SkinCluster.h"
#include "Engine/Graphics/Structs/ModelData.h"
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
	/// @return 生成されたスキンクラスター
	static CoreEngine::SkinCluster CreateSkinCluster(
		const Microsoft::WRL::ComPtr<ID3D12Device>& device,
		const Skeleton& skeleton,
		const ModelData& modelData,
		CoreEngine::DescriptorManager* descriptorManager);
	
	/// @brief スキンクラスターを更新
	/// @param skinCluster 更新するスキンクラスター
	/// @param skeleton スケルトン
	static void Update(SkinCluster& skinCluster, const Skeleton& skeleton);
};
}
