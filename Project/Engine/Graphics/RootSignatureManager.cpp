#include "RootSignatureManager.h"
#include "Engine/Graphics/Shader/ShaderReflectionData.h"

#include <cassert>
#include <map>
#include <algorithm>


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
        D3D12_ROOT_SIGNATURE_DESC desc{};
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

    void RootSignatureManager::BuildFromReflection(
        ID3D12Device* device,
        const ShaderReflectionData& reflectionData,
        bool useDescriptorTables) {

        // まず既存の設定をクリア
        Clear();

        // リソース名 -> ルートパラメータインデックスのマッピング
        std::map<std::string, UINT> rootParamMapping;
        UINT currentRootParamIndex = 0;

        // CBVを追加（常にRootDescriptorとして追加）
        for (const auto& cbv : reflectionData.GetCBVBindings()) {
            RootDescriptorConfig config;
            config.shaderRegister = cbv.bindPoint;
            config.registerSpace = cbv.space;
            config.visibility = cbv.visibility;
            AddRootCBV(config);

            // マッピングに追加（リソース名 -> ルートパラメータインデックス）
            rootParamMapping[cbv.name] = currentRootParamIndex;
            currentRootParamIndex++;
        }

        // SRVを追加
        if (useDescriptorTables && !reflectionData.GetSRVBindings().empty()) {
            // 各SRVを個別のDescriptorTableとして追加
            // （1つのテーブルにまとめると、SetGraphicsRootDescriptorTableで
            //  すべてのSRVを一度に設定する必要があるため）
            for (const auto& srv : reflectionData.GetSRVBindings()) {
                std::vector<DescriptorRangeConfig> ranges;
                DescriptorRangeConfig range;
                range.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                range.numDescriptors = srv.bindCount;
                range.baseShaderRegister = srv.bindPoint;
                range.registerSpace = srv.space;
                ranges.push_back(range);

                rootParamMapping[srv.name] = currentRootParamIndex;

                AddDescriptorTable(ranges, srv.visibility);
                currentRootParamIndex++;
            }
        } else {
            // RootDescriptorとして追加
            for (const auto& srv : reflectionData.GetSRVBindings()) {
                RootDescriptorConfig config;
                config.shaderRegister = srv.bindPoint;
                config.registerSpace = srv.space;
                config.visibility = srv.visibility;
                AddRootSRV(config);

                rootParamMapping[srv.name] = currentRootParamIndex;
                currentRootParamIndex++;
            }
        }

        // UAVを追加
        if (useDescriptorTables && !reflectionData.GetUAVBindings().empty()) {
            // SRVと同様にグループ化
            std::map<std::pair<UINT, D3D12_SHADER_VISIBILITY>, std::vector<ShaderResourceBinding>> uavGroups;
            for (const auto& uav : reflectionData.GetUAVBindings()) {
                uavGroups[{uav.space, uav.visibility}].push_back(uav);
            }

            for (auto& [key, uavs] : uavGroups) {
                std::vector<DescriptorRangeConfig> ranges;
                for (const auto& uav : uavs) {
                    DescriptorRangeConfig range;
                    range.type = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                    range.numDescriptors = uav.bindCount;
                    range.baseShaderRegister = uav.bindPoint;
                    range.registerSpace = key.first;
                    ranges.push_back(range);

                    rootParamMapping[uav.name] = currentRootParamIndex;
                }
                AddDescriptorTable(ranges, key.second);
                currentRootParamIndex++;
            }
        } else {
            for (const auto& uav : reflectionData.GetUAVBindings()) {
                RootDescriptorConfig config;
                config.shaderRegister = uav.bindPoint;
                config.registerSpace = uav.space;
                config.visibility = uav.visibility;
                AddRootUAV(config);

                rootParamMapping[uav.name] = currentRootParamIndex;
                currentRootParamIndex++;
            }
        }

        // サンプラーをStaticSamplerとして追加
        for (const auto& sampler : reflectionData.GetSamplerBindings()) {
            StaticSamplerConfig config;
            config.shaderRegister = sampler.bindPoint;
            config.registerSpace = sampler.space;
            config.visibility = sampler.visibility;
            // デフォルトのリニアサンプラー設定
            config.filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            config.addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            config.addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            config.addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            AddStaticSampler(config);
            // Samplerはルートパラメータではないため、マッピングには追加しない
        }

        // ルートシグネチャを作成
        Create(device);

        // ShaderReflectionDataにマッピングを設定
        const_cast<ShaderReflectionData&>(reflectionData).SetRootParameterMapping(rootParamMapping);

        // デバッグ出力
#ifdef _DEBUG
        logger.Log("Root Signature created from shader reflection:", LogLevel::INFO, LogCategory::Shader);
        logger.Log("  - Root Parameters: " + std::to_string(rootParameters_.size()), LogLevel::INFO, LogCategory::Shader);
        logger.Log("  - Static Samplers: " + std::to_string(staticSamplers_.size()), LogLevel::INFO, LogCategory::Shader);
        logger.Log("Resource Name -> Root Parameter Index Mapping:", LogLevel::INFO, LogCategory::Shader);
        for (const auto& [name, index] : rootParamMapping) {
            logger.Log("  - \"" + name + "\" -> " + std::to_string(index), LogLevel::INFO, LogCategory::Shader);
        }
#endif
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
