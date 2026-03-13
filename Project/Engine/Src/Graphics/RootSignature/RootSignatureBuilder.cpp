#include "RootSignatureBuilder.h"
#include "Utility/Logger/Logger.h"
#include <cassert>
#include <algorithm>
#include <set>
#include <sstream>

namespace CoreEngine
{
    RootSignatureBuildResult RootSignatureBuilder::Build(
        ID3D12Device* device,
        const ShaderReflectionData& reflectionData,
        const RootSignatureConfig& config) {

        RootSignatureBuildResult result;
        result.success = false;

        std::vector<D3D12_ROOT_PARAMETER> rootParams;
        std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> rangeStorage;
        std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;
        std::map<std::string, UINT> mapping;
        std::map<UINT, std::vector<std::string>> tableGroups;
        UINT currentIndex = 0;

        // CBVを処理
        ProcessCBVs(reflectionData, config, rootParams, mapping, currentIndex);

        // SRVを処理
        ProcessSRVs(reflectionData, config, rootParams, rangeStorage, mapping, tableGroups, currentIndex);

        // UAVを処理
        ProcessUAVs(reflectionData, config, rootParams, rangeStorage, mapping, tableGroups, currentIndex);

        // サンプラーを処理
        ProcessSamplers(reflectionData, config, staticSamplers, rootParams, rangeStorage, mapping, currentIndex);

        // Root Signatureを作成
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = static_cast<UINT>(rootParams.size());
        desc.pParameters = rootParams.data();
        desc.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
        desc.pStaticSamplers = staticSamplers.data();
        desc.Flags = config.GetFlags();

        Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, 
            signatureBlob.GetAddressOf(), errorBlob.GetAddressOf());

        if (FAILED(hr)) {
            if (errorBlob) {
                result.errorMessage = reinterpret_cast<char*>(errorBlob->GetBufferPointer());
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Shader, "{}", "RootSignature serialization failed: " + result.errorMessage);
            }
            return result;
        }

        hr = device->CreateRootSignature(0,
            signatureBlob->GetBufferPointer(),
            signatureBlob->GetBufferSize(),
            IID_PPV_ARGS(result.rootSignature.GetAddressOf()));

        if (FAILED(hr)) {
            result.errorMessage = "Failed to create RootSignature";
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Shader, "{}", result.errorMessage);
            return result;
        }

        result.resourceToRootParamIndex = std::move(mapping);
        result.tableGroupResources = std::move(tableGroups);
        result.success = true;

#ifdef _DEBUG
        // シェーダー名のタイトル作成
        std::string title = "ROOT SIGNATURE BUILD";
        const std::string& shaderName = reflectionData.GetShaderName();
        if (!shaderName.empty()) {
            title = shaderName + " - RootSignature";
        }
        
        // タイトルを中央揃え（最大幅65文字）
        const size_t boxWidth = 65;
        size_t padding = (boxWidth > title.length()) ? (boxWidth - title.length()) / 2 : 0;
        std::string centeredTitle = std::string(padding, ' ') + title;
        size_t rightPad = boxWidth - centeredTitle.length();

        // ログ出力
        std::stringstream ss;
        ss << "\n";
        ss << "┌─────────────────────────────────────────────────────────────────┐\n";
        ss << "│" << centeredTitle << std::string(rightPad, ' ') << "│\n";
        ss << "├─────────────────────────────────────────────────────────────────┤\n";
        ss << "│  Root Parameters: " << rootParams.size();
        ss << "    Static Samplers: " << staticSamplers.size() << "\n";
        ss << "├─────────────────────────────────────────────────────────────────┤\n";
        ss << "│  [Resource → RootParam Index Mapping]\n";
        for (const auto& [name, index] : result.resourceToRootParamIndex) {
            ss << "│    • " << name << "  →  [" << index << "]\n";
        }
        ss << "└─────────────────────────────────────────────────────────────────┘\n";
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Shader, "{}", ss.str());
#endif

        return result;
    }

    RootSignatureBuildResult RootSignatureBuilder::BuildManual(
        ID3D12Device* device,
        const RootSignatureConfig& config) {

        // 空のリフレクションデータで構築（手動設定のみ）
        ShaderReflectionData emptyReflection;
        return Build(device, emptyReflection, config);
    }

    void RootSignatureBuilder::ProcessCBVs(
        const ShaderReflectionData& reflectionData,
        const RootSignatureConfig& config,
        std::vector<D3D12_ROOT_PARAMETER>& rootParams,
        std::map<std::string, UINT>& mapping,
        UINT& currentIndex) {

        for (const auto& cbv : reflectionData.GetCBVBindings()) {
            BindingStrategy strategy = DetermineStrategy(cbv.name, D3D_SIT_CBUFFER, config);

            switch (strategy) {
            case BindingStrategy::RootDescriptor: {
                D3D12_ROOT_PARAMETER param = CreateRootDescriptorParam(
                    D3D12_ROOT_PARAMETER_TYPE_CBV,
                    cbv.bindPoint, cbv.space, cbv.visibility);
                rootParams.push_back(param);
                mapping[cbv.name] = currentIndex++;
                break;
            }
            case BindingStrategy::RootConstants: {
                // 設定からRoot Constantsの数を取得
                auto resourceConfig = config.GetResourceConfig(cbv.name);
                UINT num32BitValues = 1;
                if (resourceConfig && resourceConfig->rootConstantsCount) {
                    num32BitValues = *resourceConfig->rootConstantsCount;
                } else {
                    // サイズから推定（4バイト単位）
                    num32BitValues = (cbv.size + 3) / 4;
                }

                D3D12_ROOT_PARAMETER param{};
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                param.ShaderVisibility = cbv.visibility;
                param.Constants.ShaderRegister = cbv.bindPoint;
                param.Constants.RegisterSpace = cbv.space;
                param.Constants.Num32BitValues = num32BitValues;
                rootParams.push_back(param);
                mapping[cbv.name] = currentIndex++;
                break;
            }
            default:
                // CBVは基本的にRootDescriptorを使用
                D3D12_ROOT_PARAMETER param = CreateRootDescriptorParam(
                    D3D12_ROOT_PARAMETER_TYPE_CBV,
                    cbv.bindPoint, cbv.space, cbv.visibility);
                rootParams.push_back(param);
                mapping[cbv.name] = currentIndex++;
                break;
            }
        }
    }

    void RootSignatureBuilder::ProcessSRVs(
        const ShaderReflectionData& reflectionData,
        const RootSignatureConfig& config,
        std::vector<D3D12_ROOT_PARAMETER>& rootParams,
        std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>>& rangeStorage,
        std::map<std::string, UINT>& mapping,
        std::map<UINT, std::vector<std::string>>& tableGroups,
        UINT& currentIndex) {

        // グループ化されたリソースを追跡
        std::set<std::string> processedResources;

        // まずテーブルグループを処理
        for (const auto& [groupId, groupConfig] : config.GetTableGroups()) {
            std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
            D3D12_SHADER_VISIBILITY visibility = groupConfig.visibility;
            bool hasAnyResource = false;

            for (const auto& resourceName : groupConfig.resourceNames) {
                // SRVから該当リソースを検索
                const auto* srv = reflectionData.FindSRV(resourceName);
                if (srv) {
                    D3D12_DESCRIPTOR_RANGE range{};
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    range.NumDescriptors = srv->bindCount;
                    range.BaseShaderRegister = srv->bindPoint;
                    range.RegisterSpace = srv->space;
                    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                    ranges.push_back(range);

                    // 同一グループ内のリソースは同じルートパラメータを共有
                    mapping[resourceName] = currentIndex;
                    tableGroups[groupId].push_back(resourceName);
                    processedResources.insert(resourceName);
                    hasAnyResource = true;

                    // visibilityを最初のリソースから取得
                    if (ranges.size() == 1) {
                        visibility = srv->visibility;
                    }
                }
            }

            if (hasAnyResource && !ranges.empty()) {
                rangeStorage.push_back(ranges);
                D3D12_ROOT_PARAMETER param{};
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.ShaderVisibility = visibility;
                param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(rangeStorage.back().size());
                param.DescriptorTable.pDescriptorRanges = rangeStorage.back().data();
                rootParams.push_back(param);
                currentIndex++;
            }
        }

        // 残りのSRVを個別に処理
        for (const auto& srv : reflectionData.GetSRVBindings()) {
            if (processedResources.count(srv.name) > 0) {
                continue; // 既に処理済み
            }

            BindingStrategy strategy = DetermineStrategy(srv.name, D3D_SIT_TEXTURE, config);

            switch (strategy) {
            case BindingStrategy::RootDescriptor: {
                D3D12_ROOT_PARAMETER param = CreateRootDescriptorParam(
                    D3D12_ROOT_PARAMETER_TYPE_SRV,
                    srv.bindPoint, srv.space, srv.visibility);
                rootParams.push_back(param);
                mapping[srv.name] = currentIndex++;
                break;
            }
            case BindingStrategy::DescriptorTable:
            case BindingStrategy::GroupedTable:
            default: {
                // 個別のDescriptor Tableとして追加
                std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
                D3D12_DESCRIPTOR_RANGE range{};
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                range.NumDescriptors = srv.bindCount;
                range.BaseShaderRegister = srv.bindPoint;
                range.RegisterSpace = srv.space;
                range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                ranges.push_back(range);

                rangeStorage.push_back(ranges);
                D3D12_ROOT_PARAMETER param{};
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.ShaderVisibility = srv.visibility;
                param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(rangeStorage.back().size());
                param.DescriptorTable.pDescriptorRanges = rangeStorage.back().data();
                rootParams.push_back(param);
                mapping[srv.name] = currentIndex++;
                break;
            }
            }
        }
    }

    void RootSignatureBuilder::ProcessUAVs(
        const ShaderReflectionData& reflectionData,
        const RootSignatureConfig& config,
        std::vector<D3D12_ROOT_PARAMETER>& rootParams,
        std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>>& rangeStorage,
        std::map<std::string, UINT>& mapping,
        std::map<UINT, std::vector<std::string>>& tableGroups,
        UINT& currentIndex) {

        std::set<std::string> processedResources;

        // テーブルグループを処理（UAV用）
        for (const auto& [groupId, groupConfig] : config.GetTableGroups()) {
            std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
            D3D12_SHADER_VISIBILITY visibility = groupConfig.visibility;
            bool hasAnyResource = false;

            for (const auto& resourceName : groupConfig.resourceNames) {
                const auto* uav = reflectionData.FindUAV(resourceName);
                if (uav) {
                    D3D12_DESCRIPTOR_RANGE range{};
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                    range.NumDescriptors = uav->bindCount;
                    range.BaseShaderRegister = uav->bindPoint;
                    range.RegisterSpace = uav->space;
                    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                    ranges.push_back(range);

                    mapping[resourceName] = currentIndex;
                    tableGroups[groupId].push_back(resourceName);
                    processedResources.insert(resourceName);
                    hasAnyResource = true;

                    if (ranges.size() == 1) {
                        visibility = uav->visibility;
                    }
                }
            }

            if (hasAnyResource && !ranges.empty()) {
                rangeStorage.push_back(ranges);
                D3D12_ROOT_PARAMETER param{};
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.ShaderVisibility = visibility;
                param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(rangeStorage.back().size());
                param.DescriptorTable.pDescriptorRanges = rangeStorage.back().data();
                rootParams.push_back(param);
                currentIndex++;
            }
        }

        // 残りのUAVを個別に処理
        for (const auto& uav : reflectionData.GetUAVBindings()) {
            if (processedResources.count(uav.name) > 0) {
                continue;
            }

            BindingStrategy strategy = DetermineStrategy(uav.name, D3D_SIT_UAV_RWTYPED, config);

            switch (strategy) {
            case BindingStrategy::RootDescriptor: {
                D3D12_ROOT_PARAMETER param = CreateRootDescriptorParam(
                    D3D12_ROOT_PARAMETER_TYPE_UAV,
                    uav.bindPoint, uav.space, uav.visibility);
                rootParams.push_back(param);
                mapping[uav.name] = currentIndex++;
                break;
            }
            case BindingStrategy::DescriptorTable:
            case BindingStrategy::GroupedTable:
            default: {
                std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
                D3D12_DESCRIPTOR_RANGE range{};
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                range.NumDescriptors = uav.bindCount;
                range.BaseShaderRegister = uav.bindPoint;
                range.RegisterSpace = uav.space;
                range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                ranges.push_back(range);

                rangeStorage.push_back(ranges);
                D3D12_ROOT_PARAMETER param{};
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.ShaderVisibility = uav.visibility;
                param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(rangeStorage.back().size());
                param.DescriptorTable.pDescriptorRanges = rangeStorage.back().data();
                rootParams.push_back(param);
                mapping[uav.name] = currentIndex++;
                break;
            }
            }
        }
    }

    void RootSignatureBuilder::ProcessSamplers(
        const ShaderReflectionData& reflectionData,
        const RootSignatureConfig& config,
        std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers,
        std::vector<D3D12_ROOT_PARAMETER>& rootParams,
        std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>>& rangeStorage,
        std::map<std::string, UINT>& mapping,
        UINT& currentIndex) {

        for (const auto& sampler : reflectionData.GetSamplerBindings()) {
            BindingStrategy strategy = DetermineStrategy(sampler.name, D3D_SIT_SAMPLER, config);

            // サンプラー設定を取得
            SamplerConfig samplerConfig = config.GetDefaultSamplerConfig();
            auto resourceConfig = config.GetResourceConfig(sampler.name);
            if (resourceConfig && resourceConfig->samplerConfig) {
                samplerConfig = *resourceConfig->samplerConfig;
            }

            switch (strategy) {
            case BindingStrategy::StaticSampler: {
                D3D12_STATIC_SAMPLER_DESC desc = ConvertToStaticSamplerDesc(
                    samplerConfig, sampler.bindPoint, sampler.space, sampler.visibility);
                staticSamplers.push_back(desc);
                // Static Samplerはルートパラメータインデックスを持たない
                break;
            }
            case BindingStrategy::DynamicSampler: {
                // 動的サンプラーはDescriptor Tableとして追加
                std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
                D3D12_DESCRIPTOR_RANGE range{};
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                range.NumDescriptors = sampler.bindCount;
                range.BaseShaderRegister = sampler.bindPoint;
                range.RegisterSpace = sampler.space;
                range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                ranges.push_back(range);

                rangeStorage.push_back(ranges);
                D3D12_ROOT_PARAMETER param{};
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.ShaderVisibility = sampler.visibility;
                param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(rangeStorage.back().size());
                param.DescriptorTable.pDescriptorRanges = rangeStorage.back().data();
                rootParams.push_back(param);
                mapping[sampler.name] = currentIndex++;
                break;
            }
            default:
                // デフォルトはStaticSampler
                D3D12_STATIC_SAMPLER_DESC desc = ConvertToStaticSamplerDesc(
                    samplerConfig, sampler.bindPoint, sampler.space, sampler.visibility);
                staticSamplers.push_back(desc);
                break;
            }
        }
    }

    BindingStrategy RootSignatureBuilder::DetermineStrategy(
        const std::string& resourceName,
        D3D_SHADER_INPUT_TYPE resourceType,
        const RootSignatureConfig& config) const {

        // 個別設定を優先
        auto resourceConfig = config.GetResourceConfig(resourceName);
        if (resourceConfig) {
            return resourceConfig->strategy;
        }

        // デフォルト戦略を返す
        switch (resourceType) {
        case D3D_SIT_CBUFFER:
            return config.GetDefaultCBVStrategy();
        case D3D_SIT_TEXTURE:
        case D3D_SIT_STRUCTURED:
        case D3D_SIT_BYTEADDRESS:
            return config.GetDefaultSRVStrategy();
        case D3D_SIT_UAV_RWTYPED:
        case D3D_SIT_UAV_RWSTRUCTURED:
        case D3D_SIT_UAV_RWBYTEADDRESS:
            return config.GetDefaultUAVStrategy();
        case D3D_SIT_SAMPLER:
            return config.GetDefaultSamplerStrategy();
        default:
            return BindingStrategy::DescriptorTable;
        }
    }

    D3D12_STATIC_SAMPLER_DESC RootSignatureBuilder::ConvertToStaticSamplerDesc(
        const SamplerConfig& config,
        UINT shaderRegister,
        UINT registerSpace,
        D3D12_SHADER_VISIBILITY visibility) const {

        D3D12_STATIC_SAMPLER_DESC desc{};
        desc.Filter = config.filter;
        desc.AddressU = config.addressU;
        desc.AddressV = config.addressV;
        desc.AddressW = config.addressW;
        desc.MipLODBias = config.mipLODBias;
        desc.MaxAnisotropy = config.maxAnisotropy;
        desc.ComparisonFunc = config.comparisonFunc;
        desc.BorderColor = config.borderColor;
        desc.MinLOD = config.minLOD;
        desc.MaxLOD = config.maxLOD;
        desc.ShaderRegister = shaderRegister;
        desc.RegisterSpace = registerSpace;
        desc.ShaderVisibility = visibility;
        return desc;
    }

    D3D12_ROOT_PARAMETER RootSignatureBuilder::CreateRootDescriptorParam(
        D3D12_ROOT_PARAMETER_TYPE type,
        UINT shaderRegister,
        UINT registerSpace,
        D3D12_SHADER_VISIBILITY visibility) const {

        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = type;
        param.ShaderVisibility = visibility;
        param.Descriptor.ShaderRegister = shaderRegister;
        param.Descriptor.RegisterSpace = registerSpace;
        return param;
    }

    D3D12_ROOT_PARAMETER RootSignatureBuilder::CreateDescriptorTableParam(
        const std::vector<D3D12_DESCRIPTOR_RANGE>& ranges,
        D3D12_SHADER_VISIBILITY visibility) const {

        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.ShaderVisibility = visibility;
        param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(ranges.size());
        param.DescriptorTable.pDescriptorRanges = ranges.data();
        return param;
    }
}


