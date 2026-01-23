#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include "MathCore.h"

namespace CoreEngine
{
// 前方宣言
class DirectXCommon;
class ResourceFactory;

/// @brief GPU送信用パーティクルデータ
struct ParticleForGPU {
	Matrix4x4 WVP;
	Matrix4x4 World;
	Vector4 color;
};

/// @brief パーティクルシステムのリソース管理クラス
/// GPUリソース（インスタンシングバッファ、SRV）の生成と管理を担当
class ParticleResourceManager {
public:
	ParticleResourceManager() = default;
	~ParticleResourceManager() = default;

	/// @brief 初期化
	/// @param dxCommon DirectXCommon
	/// @param resourceFactory リソースファクトリ
	/// @param maxInstances 最大インスタンス数
	void Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory, uint32_t maxInstances);

	/// @brief インスタンシングデータへのポインタを取得
	/// @return インスタンシングデータのポインタ
	ParticleForGPU* GetInstancingData() { return instancingData_; }

	/// @brief SRVのGPUハンドルを取得
	/// @return SRVのGPUディスクリプタハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU() const { return srvHandleGPU_; }

	/// @brief SRVのCPUハンドルを取得
	/// @return SRVのCPUディスクリプタハンドル
  D3D12_CPU_DESCRIPTOR_HANDLE GetSrvHandleCPU() const { return srvHandleCPU_; }

private:
	/// @brief インスタンシングリソースを作成
	/// @param maxInstances 最大インスタンス数
	void CreateInstancingResource(uint32_t maxInstances);

	/// @brief SRVを作成
  /// @param maxInstances 最大インスタンス数
	void CreateSRV(uint32_t maxInstances);

	// DirectX関連
	DirectXCommon* dxCommon_ = nullptr;
	ResourceFactory* resourceFactory_ = nullptr;

	// GPUリソース
  Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU_ = {};
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU_ = {};
	ParticleForGPU* instancingData_ = nullptr;
};

} // namespace CoreEngine
