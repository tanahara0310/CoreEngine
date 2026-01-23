#pragma once

#include <d3d12.h>
#include <vector>
#include <wrl.h>

#include "Engine/Utility/Logger/Logger.h"

// RootSignatureの管理クラス

namespace CoreEngine
{
class RootSignatureManager {
public:
	/// @brief ルートパラメータの種類
	enum class RootParameterType {
		ConstantBufferView,      // CBV (Direct)
		ShaderResourceView,      // SRV (Direct)
		UnorderedAccessView,     // UAV (Direct)
		DescriptorTable,         // Descriptor Table
		Constants                // 32bit Constants
	};

	/// @brief ディスクリプタレンジの設定情報
	struct DescriptorRangeConfig {
		D3D12_DESCRIPTOR_RANGE_TYPE type;  // SRV, UAV, CBV, Sampler
		UINT numDescriptors;               // ディスクリプタの数
		UINT baseShaderRegister;           // 開始レジスタ番号 (例: t0, u0, b0)
		UINT registerSpace;                // レジスタ空間 (通常は0)
		UINT offsetInDescriptorsFromTableStart; // テーブル内のオフセット

		DescriptorRangeConfig()
			: type(D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
			, numDescriptors(1)
			, baseShaderRegister(0)
			, registerSpace(0)
			, offsetInDescriptorsFromTableStart(D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
		{}
	};

	/// @brief ルートディスクリプタの設定情報 (CBV, SRV, UAV Direct)
	struct RootDescriptorConfig {
		UINT shaderRegister;    // レジスタ番号 (例: b0, t0, u0)
		UINT registerSpace;     // レジスタ空間 (通常は0)
		D3D12_SHADER_VISIBILITY visibility; // シェーダー可視性

		RootDescriptorConfig()
			: shaderRegister(0)
			, registerSpace(0)
			, visibility(D3D12_SHADER_VISIBILITY_ALL)
		{}
	};

	/// @brief ルート定数の設定情報
	struct RootConstantsConfig {
		UINT shaderRegister;    // レジスタ番号 (b0, b1, ...)
		UINT registerSpace;     // レジスタ空間 (通常は0)
		UINT num32BitValues;    // 32bitの値の数
		D3D12_SHADER_VISIBILITY visibility; // シェーダー可視性

		RootConstantsConfig()
			: shaderRegister(0)
			, registerSpace(0)
			, num32BitValues(1)
			, visibility(D3D12_SHADER_VISIBILITY_ALL)
		{}
	};

	/// @brief スタティックサンプラーの設定情報
	struct StaticSamplerConfig {
		UINT shaderRegister;             // レジスタ番号 (s0, s1, ...)
		UINT registerSpace;              // レジスタ空間 (通常は0)
		D3D12_SHADER_VISIBILITY visibility; // シェーダー可視性
		D3D12_FILTER filter;             // フィルタ設定
		D3D12_TEXTURE_ADDRESS_MODE addressU; // U方向のアドレスモード
		D3D12_TEXTURE_ADDRESS_MODE addressV; // V方向のアドレスモード
		D3D12_TEXTURE_ADDRESS_MODE addressW; // W方向のアドレスモード
		D3D12_COMPARISON_FUNC comparisonFunc; // 比較関数
		D3D12_STATIC_BORDER_COLOR borderColor; // ボーダーカラー
		FLOAT mipLODBias;                // MipLODバイアス
		UINT maxAnisotropy;              // 異方性フィルタリングの最大値
		FLOAT minLOD;                    // 最小LOD
		FLOAT maxLOD;                    // 最大LOD

		StaticSamplerConfig()
			: shaderRegister(0)
			, registerSpace(0)
			, visibility(D3D12_SHADER_VISIBILITY_PIXEL)
			, filter(D3D12_FILTER_MIN_MAG_MIP_LINEAR)
			, addressU(D3D12_TEXTURE_ADDRESS_MODE_WRAP)
			, addressV(D3D12_TEXTURE_ADDRESS_MODE_WRAP)
			, addressW(D3D12_TEXTURE_ADDRESS_MODE_WRAP)
			, comparisonFunc(D3D12_COMPARISON_FUNC_NEVER)
			, borderColor(D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK)
			, mipLODBias(0.0f)
			, maxAnisotropy(16)
			, minLOD(0.0f)
			, maxLOD(D3D12_FLOAT32_MAX)
		{}
	};

	// RootSignatureの取得
	ID3D12RootSignature* GetRootSignature() { return rootSignature_.Get(); }

	/// @brief CBVをルートディスクリプタとして追加 (Direct binding)
	/// @param config CBVの設定情報
	void AddRootCBV(const RootDescriptorConfig& config);

	/// @brief SRVをルートディスクリプタとして追加 (Direct binding)
	/// @param config SRVの設定情報
	void AddRootSRV(const RootDescriptorConfig& config);

	/// @brief UAVをルートディスクリプタとして追加 (Direct binding)
	/// @param config UAVの設定情報
	void AddRootUAV(const RootDescriptorConfig& config);

	/// @brief ディスクリプタテーブルを追加
	/// @param ranges ディスクリプタレンジの配列
	/// @param visibility シェーダー可視性
	void AddDescriptorTable(const std::vector<DescriptorRangeConfig>& ranges, D3D12_SHADER_VISIBILITY visibility);

	/// @brief ルート定数を追加 (32bit定数の配列)
	/// @param config ルート定数の設定情報
	void AddRootConstants(const RootConstantsConfig& config);

	/// @brief スタティックサンプラーを追加
	/// @param config サンプラーの設定情報
	void AddStaticSampler(const StaticSamplerConfig& config);

	/// @brief デフォルトのリニアサンプラーを追加
	/// @param shaderRegister レジスタ番号 (s0, s1, ...)
	/// @param visibility シェーダー可視性
	/// @param registerSpace レジスタ空間 (デフォルト: 0)
	void AddDefaultLinearSampler(UINT shaderRegister, D3D12_SHADER_VISIBILITY visibility, UINT registerSpace = 0);

	/// @brief フラグの設定
	/// @param flags ルートシグネチャのフラグ
	void SetFlags(D3D12_ROOT_SIGNATURE_FLAGS flags) { flags_ = flags; }

	/// @brief ルートシグネチャを作成
	/// @param device D3D12デバイス
	void Create(ID3D12Device* device);

	/// @brief 設定をクリア (再利用する場合)
	void Clear();

	// 互換性のための旧メソッド (非推奨)
	[[deprecated("Use AddRootCBV with RootDescriptorConfig instead")]]
	void SetCBV(UINT shaderRegister, D3D12_SHADER_VISIBILITY visibility);

	[[deprecated("Use AddDescriptorTable with DescriptorRangeConfig instead")]]
	void SetSRV(UINT shaderRegister, D3D12_SHADER_VISIBILITY visibility);

	[[deprecated("Use AddStaticSampler or AddDefaultLinearSampler instead")]]
	void SetSampler(const D3D12_STATIC_SAMPLER_DESC& sampler);

	[[deprecated("Use AddDefaultLinearSampler instead")]]
	void SetDefaultSampler(UINT shaderRegister, D3D12_SHADER_VISIBILITY shadervisibility);

private:
	Logger& logger = Logger::GetInstance();

	// RootSignature
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
	// シリアライズしたバイナリ
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob_ = nullptr;
	// エラー情報
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob_ = nullptr;

	std::vector<D3D12_ROOT_PARAMETER> rootParameters_;
	// ディスクリプタレンジのストレージ (生存期間の管理)
	std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> descriptorRanges_;
	// StaticSamplerの設定
	std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers_;

	// フラグ
	D3D12_ROOT_SIGNATURE_FLAGS flags_ = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	/// @brief ルートディスクリプタを追加する内部実装
	void AddRootDescriptor(D3D12_ROOT_PARAMETER_TYPE type, const RootDescriptorConfig& config);
};
}
