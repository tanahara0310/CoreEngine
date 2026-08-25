#include "pch.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
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

        // SRV ヒープはフレーム先頭で CommandContext が 1 回バインドする（個別バインドは不要）
        (void)srvHeap;

        // 出力バッファを前フレームの状態からUAVへ戻す（初回はUNORDERED_ACCESSのままなので無発行）
        Barrier::Transition(cmdList, skinCluster.outputVertexResource,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetComputeRootSignature(rootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        if (sourceVerticesIdx_ >= 0) {
            cmdList->SetComputeRootDescriptorTable(sourceVerticesIdx_, skinCluster.sourceVertexSrvHandle.gpuHandle);
        }
        if (influencesIdx_ >= 0) {
            cmdList->SetComputeRootDescriptorTable(influencesIdx_, skinCluster.influenceSrvHandle.gpuHandle);
        }
        if (matrixPaletteIdx_ >= 0) {
            cmdList->SetComputeRootDescriptorTable(matrixPaletteIdx_, skinCluster.paletteSrvHandle.gpuHandle);
        }
        if (outputVerticesIdx_ >= 0) {
            cmdList->SetComputeRootDescriptorTable(outputVerticesIdx_, skinCluster.outputUavHandle.gpuHandle);
        }
        if (skinningParamsIdx_ >= 0) {
            cmdList->SetComputeRootConstantBufferView(skinningParamsIdx_, skinCluster.skinningParamsCB->GetGPUVirtualAddress());
        }

        const UINT groupCount = (vertexCount + 63) / 64;
        cmdList->Dispatch(groupCount, 1, 1);

        // 出力バッファを頂点バッファとして読めるように遷移
        BarrierBatch batch(cmdList);
        batch.UAV(skinCluster.outputVertexResource);
        batch.Transition(skinCluster.outputVertexResource,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    }
}
