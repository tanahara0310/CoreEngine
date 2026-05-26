#include "pch.h"
#include "GlobalRootSignatureManager.h"
#include "Utility/Logger/Logger.h"

#include <cassert>
#include <sstream>

namespace CoreEngine
{
    // =========================================================================
    // エントリ追加（流暢インターフェース）
    // =========================================================================
    GlobalRootSignatureManager& GlobalRootSignatureManager::AddUAVTable(
        const std::string& name, UINT shaderRegister, UINT registerSpace)
    {
        entries_.push_back({ name, DXRRootEntryType::UAVTable, shaderRegister, registerSpace });
        return *this;
    }

    GlobalRootSignatureManager& GlobalRootSignatureManager::AddSRVTable(
        const std::string& name, UINT shaderRegister, UINT registerSpace)
    {
        entries_.push_back({ name, DXRRootEntryType::SRVTable, shaderRegister, registerSpace });
        return *this;
    }

    GlobalRootSignatureManager& GlobalRootSignatureManager::AddCBV(
        const std::string& name, UINT shaderRegister, UINT registerSpace)
    {
        entries_.push_back({ name, DXRRootEntryType::CBV, shaderRegister, registerSpace, 0 });
        return *this;
    }

    GlobalRootSignatureManager& GlobalRootSignatureManager::AddRootConstants(
        const std::string& name, UINT shaderRegister, UINT num32BitValues, UINT registerSpace)
    {
        entries_.push_back({ name, DXRRootEntryType::RootConstants, shaderRegister, registerSpace, num32BitValues });
        return *this;
    }

    // =========================================================================
    // Build
    // =========================================================================
    bool GlobalRootSignatureManager::Build(ID3D12Device* device)
    {
        assert(device);
        assert(!entries_.empty());

        nameToIndex_.clear();
        rootSignature_.Reset();

        // デスクリプタレンジをエントリと同じ寿命で保持する
        // （D3D12_ROOT_PARAMETER がポインタで参照するため Build 完了まで必要）
        std::vector<D3D12_DESCRIPTOR_RANGE> rangeStorage;
        rangeStorage.reserve(entries_.size());

        std::vector<D3D12_ROOT_PARAMETER> params;
        params.reserve(entries_.size());

        for (UINT i = 0; i < static_cast<UINT>(entries_.size()); ++i) {
            const Entry& entry = entries_[i];
            nameToIndex_[entry.name] = i;

            D3D12_ROOT_PARAMETER param{};
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            switch (entry.type) {
            case DXRRootEntryType::UAVTable: {
                D3D12_DESCRIPTOR_RANGE range{};
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                range.NumDescriptors = 1;
                range.BaseShaderRegister = entry.shaderRegister;
                range.RegisterSpace = entry.registerSpace;
                range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                rangeStorage.push_back(range);

                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.DescriptorTable.NumDescriptorRanges = 1;
                param.DescriptorTable.pDescriptorRanges = &rangeStorage.back();
                break;
            }
            case DXRRootEntryType::SRVTable: {
                D3D12_DESCRIPTOR_RANGE range{};
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                range.NumDescriptors = 1;
                range.BaseShaderRegister = entry.shaderRegister;
                range.RegisterSpace = entry.registerSpace;
                range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                rangeStorage.push_back(range);

                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.DescriptorTable.NumDescriptorRanges = 1;
                param.DescriptorTable.pDescriptorRanges = &rangeStorage.back();
                break;
            }
            case DXRRootEntryType::CBV:
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                param.Descriptor.ShaderRegister = entry.shaderRegister;
                param.Descriptor.RegisterSpace = entry.registerSpace;
                break;
            case DXRRootEntryType::RootConstants:
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                param.Constants.ShaderRegister = entry.shaderRegister;
                param.Constants.RegisterSpace = entry.registerSpace;
                param.Constants.Num32BitValues = entry.num32BitValues;
                break;
            }

            params.push_back(param);
        }

        // ルートシグネチャ記述子
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = static_cast<UINT>(params.size());
        desc.pParameters = params.data();
        desc.NumStaticSamplers = 0;
        desc.pStaticSamplers = nullptr;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1,
            signatureBlob.GetAddressOf(), errorBlob.GetAddressOf());

        if (FAILED(hr)) {
            if (errorBlob) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                    "GlobalRootSignatureManager: Serialize failed: {}",
                    static_cast<const char*>(errorBlob->GetBufferPointer()));
            }
            return false;
        }

        hr = device->CreateRootSignature(
            0,
            signatureBlob->GetBufferPointer(),
            signatureBlob->GetBufferSize(),
            IID_PPV_ARGS(rootSignature_.GetAddressOf()));

        if (FAILED(hr)) {
            Logger::GetInstance().Log(
                "GlobalRootSignatureManager: CreateRootSignature failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

#ifdef _DEBUG
        // 構築結果ログ
        std::stringstream ss;
        ss << "\n";
        ss << "┌─────────────────────────────────────────────────────────────────┐\n";
        ss << "│          GLOBAL ROOT SIGNATURE BUILD (DXR)                      │\n";
        ss << "├─────────────────────────────────────────────────────────────────┤\n";
        ss << "│  Root Parameters: " << params.size() << "\n";
        ss << "├─────────────────────────────────────────────────────────────────┤\n";
        ss << "│  [Entry → RootParam Index Mapping]\n";
        for (const auto& [name, index] : nameToIndex_) {
            const Entry& e = entries_[index];
            const char* typeStr =
                e.type == DXRRootEntryType::UAVTable ? "UAVTable" :
                e.type == DXRRootEntryType::SRVTable ? "SRVTable" : "CBV";
            ss << "│    • [" << index << "] " << typeStr
                << "  reg=" << e.shaderRegister
                << "  space=" << e.registerSpace
                << "  \"" << name << "\"\n";
        }
        ss << "└─────────────────────────────────────────────────────────────────┘\n";
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", ss.str());
#endif

        return true;
    }

    // =========================================================================
    // アクセサ
    // =========================================================================
    int GlobalRootSignatureManager::GetRootParameterIndex(const std::string& name) const
    {
        auto it = nameToIndex_.find(name);
        if (it != nameToIndex_.end()) {
            return static_cast<int>(it->second);
        }
        return -1;
    }

    void GlobalRootSignatureManager::Clear()
    {
        entries_.clear();
        nameToIndex_.clear();
        rootSignature_.Reset();
    }
}
