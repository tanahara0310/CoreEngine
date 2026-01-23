#include "RootSignatureManager.h"

#include <cassert>


namespace CoreEngine
{
void RootSignatureManager::AddRootCBV(const RootDescriptorConfig& config)
{
	AddRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, config);
}

void RootSignatureManager::AddRootSRV(const RootDescriptorConfig& config)
{
	AddRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_SRV, config);
}

void RootSignatureManager::AddRootUAV(const RootDescriptorConfig& config)
{
	AddRootDescriptor(D3D12_ROOT_PARAMETER_TYPE_UAV, config);
}

void RootSignatureManager::AddRootDescriptor(D3D12_ROOT_PARAMETER_TYPE type, const RootDescriptorConfig& config)
{
	D3D12_ROOT_PARAMETER rootParameter = {};
	rootParameter.ParameterType = type;
	rootParameter.ShaderVisibility = config.visibility;
	rootParameter.Descriptor.ShaderRegister = config.shaderRegister;
	rootParameter.Descriptor.RegisterSpace = config.registerSpace;
	rootParameters_.push_back(rootParameter);
}

void RootSignatureManager::AddDescriptorTable(const std::vector<DescriptorRangeConfig>& ranges, D3D12_SHADER_VISIBILITY visibility)
{
	if (ranges.empty()) {
		logger.Log("Warning: AddDescriptorTable called with empty ranges");
		return;
	}

	// 新しいDescriptorRangeセットを追加
	descriptorRanges_.emplace_back();
	auto& d3d12Ranges = descriptorRanges_.back();
	d3d12Ranges.reserve(ranges.size());

	// 設定をD3D12構造体に変換
	for (const auto& rangeConfig : ranges) {
		D3D12_DESCRIPTOR_RANGE range = {};
		range.RangeType = rangeConfig.type;
		range.NumDescriptors = rangeConfig.numDescriptors;
		range.BaseShaderRegister = rangeConfig.baseShaderRegister;
		range.RegisterSpace = rangeConfig.registerSpace;
		range.OffsetInDescriptorsFromTableStart = rangeConfig.offsetInDescriptorsFromTableStart;
		d3d12Ranges.push_back(range);
	}

	// ルートパラメータを追加
	D3D12_ROOT_PARAMETER rootParameter = {};
	rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameter.ShaderVisibility = visibility;
	rootParameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(d3d12Ranges.size());
	rootParameter.DescriptorTable.pDescriptorRanges = d3d12Ranges.data();

	rootParameters_.push_back(rootParameter);
}

void RootSignatureManager::AddRootConstants(const RootConstantsConfig& config)
{
	D3D12_ROOT_PARAMETER rootParameter = {};
	rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameter.ShaderVisibility = config.visibility;
	rootParameter.Constants.ShaderRegister = config.shaderRegister;
	rootParameter.Constants.RegisterSpace = config.registerSpace;
	rootParameter.Constants.Num32BitValues = config.num32BitValues;
	rootParameters_.push_back(rootParameter);
}

void RootSignatureManager::AddStaticSampler(const StaticSamplerConfig& config)
{
	D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = config.filter;
	samplerDesc.AddressU = config.addressU;
	samplerDesc.AddressV = config.addressV;
	samplerDesc.AddressW = config.addressW;
	samplerDesc.MipLODBias = config.mipLODBias;
	samplerDesc.MaxAnisotropy = config.maxAnisotropy;
	samplerDesc.ComparisonFunc = config.comparisonFunc;
	samplerDesc.BorderColor = config.borderColor;
	samplerDesc.MinLOD = config.minLOD;
	samplerDesc.MaxLOD = config.maxLOD;
	samplerDesc.ShaderRegister = config.shaderRegister;
	samplerDesc.RegisterSpace = config.registerSpace;
	samplerDesc.ShaderVisibility = config.visibility;

	staticSamplers_.push_back(samplerDesc);
}

void RootSignatureManager::AddDefaultLinearSampler(UINT shaderRegister, D3D12_SHADER_VISIBILITY visibility, UINT registerSpace)
{
	StaticSamplerConfig config;
	config.shaderRegister = shaderRegister;
	config.registerSpace = registerSpace;
	config.visibility = visibility;
	config.filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	config.addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	config.addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	config.addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	config.comparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

	AddStaticSampler(config);
}

void RootSignatureManager::Create(ID3D12Device* device)
{
	D3D12_ROOT_SIGNATURE_DESC desc {};
	desc.NumParameters = static_cast<UINT>(rootParameters_.size());
	desc.pParameters = rootParameters_.data();
	desc.NumStaticSamplers = static_cast<UINT>(staticSamplers_.size());
	desc.pStaticSamplers = staticSamplers_.data();
	desc.Flags = flags_;

	// シリアライズしてバイナリにする
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob_, &errorBlob_);
	if (FAILED(hr)) {
		logger.Log(reinterpret_cast<char*>(errorBlob_->GetBufferPointer()));
		assert(false);
	}

	// バイナリを元に生成
	hr = device->CreateRootSignature(0,
		signatureBlob_->GetBufferPointer(),
		signatureBlob_->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));
}

void RootSignatureManager::Clear()
{
	rootParameters_.clear();
	descriptorRanges_.clear();
	staticSamplers_.clear();
	rootSignature_.Reset();
	signatureBlob_.Reset();
	errorBlob_.Reset();
}

// 互換性のための旧メソッドの実装
void RootSignatureManager::SetCBV(UINT shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
	RootDescriptorConfig config;
	config.shaderRegister = shaderRegister;
	config.visibility = visibility;
	config.registerSpace = 0;
	AddRootCBV(config);
}

void RootSignatureManager::SetSRV(UINT shaderRegister, D3D12_SHADER_VISIBILITY visibility)
{
	DescriptorRangeConfig rangeConfig;
	rangeConfig.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	rangeConfig.numDescriptors = 1;
	rangeConfig.baseShaderRegister = shaderRegister;
	rangeConfig.registerSpace = 0;
	rangeConfig.offsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	AddDescriptorTable({ rangeConfig }, visibility);
}

void RootSignatureManager::SetSampler(const D3D12_STATIC_SAMPLER_DESC& sampler)
{
	staticSamplers_.push_back(sampler);
}

void RootSignatureManager::SetDefaultSampler(UINT shaderRegister, D3D12_SHADER_VISIBILITY shadervisibility)
{
	AddDefaultLinearSampler(shaderRegister, shadervisibility, 0);
}
}
