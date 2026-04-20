#include "RayTracingPipelineBuilder.h"
#include "Utility/Logger/Logger.h"

#include <cassert>
#include <vector>

namespace CoreEngine
{
    RayTracingPipelineBuilder& RayTracingPipelineBuilder::SetDXILLibrary(IDxcBlob* shaderBlob)
    {
        assert(shaderBlob);
        shaderBlob_ = shaderBlob;
        return *this;
    }

    RayTracingPipelineBuilder& RayTracingPipelineBuilder::AddHitGroup(const DXRHitGroupDesc& desc)
    {
        assert(desc.name);
        hitGroups_.push_back(desc);
        return *this;
    }

    RayTracingPipelineBuilder& RayTracingPipelineBuilder::SetShaderConfig(
        UINT maxPayloadSizeInBytes, UINT maxAttributeSizeInBytes)
    {
        maxPayloadSize_ = maxPayloadSizeInBytes;
        maxAttributeSize_ = maxAttributeSizeInBytes;
        return *this;
    }

    RayTracingPipelineBuilder& RayTracingPipelineBuilder::SetGlobalRootSignature(
        ID3D12RootSignature* rootSignature)
    {
        assert(rootSignature);
        globalRootSig_ = rootSignature;
        return *this;
    }

    RayTracingPipelineBuilder& RayTracingPipelineBuilder::SetMaxRecursionDepth(UINT depth)
    {
        maxRecursionDepth_ = depth;
        return *this;
    }

    bool RayTracingPipelineBuilder::Build(
        ID3D12Device* device,
        Microsoft::WRL::ComPtr<ID3D12StateObject>& outStateObject,
        Microsoft::WRL::ComPtr<ID3D12StateObjectProperties>& outProperties)
    {
        assert(device);
        assert(shaderBlob_);
        assert(globalRootSig_);
        assert(maxPayloadSize_ > 0);

        // ポインタ参照を持つ記述子構造体を先に確保し、subobjects より長い寿命を保証する
        D3D12_DXIL_LIBRARY_DESC           libDesc{};
        std::vector<D3D12_HIT_GROUP_DESC> hitGroupDescs(hitGroups_.size());
        D3D12_RAYTRACING_SHADER_CONFIG    shaderConfig{};
        D3D12_GLOBAL_ROOT_SIGNATURE       globalRS{};
        D3D12_RAYTRACING_PIPELINE_CONFIG  pipelineConfig{};

        std::vector<D3D12_STATE_SUBOBJECT> subObjects;
        subObjects.reserve(3 + hitGroups_.size() + 2);

        // 1. DXIL Library
        libDesc.DXILLibrary.pShaderBytecode = shaderBlob_->GetBufferPointer();
        libDesc.DXILLibrary.BytecodeLength = shaderBlob_->GetBufferSize();
        libDesc.NumExports = 0;  // 全エクスポート
        subObjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &libDesc });

        // 2. Hit Groups
        for (size_t i = 0; i < hitGroups_.size(); ++i) {
            const DXRHitGroupDesc& src = hitGroups_[i];
            D3D12_HIT_GROUP_DESC& dst = hitGroupDescs[i];
            dst.HitGroupExport = src.name;
            dst.Type = src.type;
            dst.ClosestHitShaderImport = src.closestHitShader;
            dst.AnyHitShaderImport = src.anyHitShader;
            dst.IntersectionShaderImport = src.intersectionShader;
            subObjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &dst });
        }

        // 3. Shader Config
        shaderConfig.MaxPayloadSizeInBytes = maxPayloadSize_;
        shaderConfig.MaxAttributeSizeInBytes = maxAttributeSize_;
        subObjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shaderConfig });

        // 4. Global Root Signature
        globalRS.pGlobalRootSignature = globalRootSig_;
        subObjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &globalRS });

        // 5. Pipeline Config
        pipelineConfig.MaxTraceRecursionDepth = maxRecursionDepth_;
        subObjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipelineConfig });

        // State Object 作成
        D3D12_STATE_OBJECT_DESC stateObjectDesc{};
        stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        stateObjectDesc.NumSubobjects = static_cast<UINT>(subObjects.size());
        stateObjectDesc.pSubobjects = subObjects.data();

        Microsoft::WRL::ComPtr<ID3D12Device5> device5;
        HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&device5));
        if (FAILED(hr)) {
            Logger::GetInstance().Log(
                "RayTracingPipelineBuilder: ID3D12Device5 not available",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        hr = device5->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&outStateObject));
        if (FAILED(hr)) {
            Logger::GetInstance().Log(
                "RayTracingPipelineBuilder: CreateStateObject failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        hr = outStateObject->QueryInterface(IID_PPV_ARGS(&outProperties));
        return SUCCEEDED(hr);
    }
}
