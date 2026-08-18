#include "pch.h"
#include "SkinningComputeDispatcher.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "SkinCluster.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include <cassert>
#include <stdexcept>

namespace CoreEngine
{
    void SkinningComputeDispatcher::Initialize(ID3D12Device* device)
    {
        assert(device);

        shaderCompiler_->Initialize();
        IDxcBlob* csBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Skinning/Skinning.CS.hlsl", L"cs_6_0");
        assert(csBlob != nullptr);

        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        reflectionData_ = reflectionBuilder_->BuildFromComputeShader(csBlob, "SkinningComputeDispatcher");

        // RootSignature構成: CBVはRootDescriptor、SRV/UAVはDescriptorTable
        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);
        config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
        config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);

        auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error("Failed to create Skinning Compute RootSignature: " + buildResult.errorMessage);
        }

        computePso_ = ComputePipelineUtil::Create(
            device, rootSignatureMg_->GetRootSignature(), csBlob, "SkinningCompute");
        if (!computePso_) {
            throw std::runtime_error("Failed to create Skinning Compute PSO");
        }

        sourceVerticesIdx_ = reflectionData_->GetRootParameterIndexByName("gSourceVertices");
        influencesIdx_ = reflectionData_->GetRootParameterIndexByName("gInfluences");
        matrixPaletteIdx_ = reflectionData_->GetRootParameterIndexByName("gMatrixPalette");
        outputVerticesIdx_ = reflectionData_->GetRootParameterIndexByName("gOutputVertices");
        skinningParamsIdx_ = reflectionData_->GetRootParameterIndexByName("SkinningParams");
    }

    void SkinningComputeDispatcher::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12DescriptorHeap* srvHeap,
        SkinCluster& skinCluster,
        UINT vertexCount)
    {
        assert(cmdList);
        if (vertexCount == 0) {
            return;
        }

        // 単体Compute呼び出しでもディスクリプタヒープが確実にバインドされているようにする
        ID3D12DescriptorHeap* heaps[] = { srvHeap };
        cmdList->SetDescriptorHeaps(1, heaps);

        // 出力バッファを前フレームの状態からUAVへ戻す（初回はUNORDERED_ACCESSのまま）
        if (skinCluster.outputBufferState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = skinCluster.outputVertexResource.Get();
            barrier.Transition.StateBefore = skinCluster.outputBufferState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &barrier);
            skinCluster.outputBufferState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        cmdList->SetComputeRootSignature(rootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        if (sourceVerticesIdx_ >= 0) {
            cmdList->SetComputeRootDescriptorTable(sourceVerticesIdx_, skinCluster.sourceVertexSrvHandle.second);
        }
        if (influencesIdx_ >= 0) {
            cmdList->SetComputeRootDescriptorTable(influencesIdx_, skinCluster.influenceSrvHandle.second);
        }
        if (matrixPaletteIdx_ >= 0) {
            cmdList->SetComputeRootDescriptorTable(matrixPaletteIdx_, skinCluster.paletteSrvHandle.second);
        }
        if (outputVerticesIdx_ >= 0) {
            cmdList->SetComputeRootDescriptorTable(outputVerticesIdx_, skinCluster.outputUavHandle.second);
        }
        if (skinningParamsIdx_ >= 0) {
            cmdList->SetComputeRootConstantBufferView(skinningParamsIdx_, skinCluster.skinningParamsCB->GetGPUVirtualAddress());
        }

        const UINT groupCount = (vertexCount + 63) / 64;
        cmdList->Dispatch(groupCount, 1, 1);

        // 出力バッファを頂点バッファとして読めるように遷移
        D3D12_RESOURCE_BARRIER uavBarrier{};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = skinCluster.outputVertexResource.Get();
        cmdList->ResourceBarrier(1, &uavBarrier);

        D3D12_RESOURCE_BARRIER transitionBarrier{};
        transitionBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transitionBarrier.Transition.pResource = skinCluster.outputVertexResource.Get();
        transitionBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        transitionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        transitionBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &transitionBarrier);
        skinCluster.outputBufferState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    }
}
