#include "pch.h"
#include "FFTOceanDispatchHelper.h"

#include "Graphics/Common/ResourceBarrierHelper.h"

namespace CoreEngine
{
    void FFTOceanDispatchHelper::DispatchEvolutionPass(
        ID3D12GraphicsCommandList* cmdList,
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>& spectrumTextureA,
        std::array<D3D12_RESOURCE_STATES, 2>& spectrumAState,
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>& spectrumTextureB,
        std::array<D3D12_RESOURCE_STATES, 2>& spectrumBState,
        CustomShaderPipeline& evolutionPipeline,
        D3D12_GPU_DESCRIPTOR_HANDLE spectrumSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE spectrumAUavHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE spectrumBUavHandle,
        ID3D12Resource* simulationConstantsBuffer,
        uint32_t resolution)
    {
        for (uint32_t i = 0; i < 2; ++i) {
            ResourceBarrierHelper::Transition(
                cmdList,
                spectrumTextureA[i].Get(),
                spectrumAState[i],
                i == 0 ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COMMON);
            ResourceBarrierHelper::Transition(
                cmdList,
                spectrumTextureB[i].Get(),
                spectrumBState[i],
                i == 0 ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COMMON);
        }

        cmdList->SetPipelineState(evolutionPipeline.GetComputePSO());
        cmdList->SetComputeRootSignature(evolutionPipeline.GetComputeRootSignature());

        const int spectrumSlot = evolutionPipeline.GetComputeRootParamIndex("gSpectrumSamples");
        if (spectrumSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumSlot), spectrumSrvHandle);
        }

        const int spectrumAOutputSlot = evolutionPipeline.GetComputeRootParamIndex("gHeightDisplacementXOutput");
        if (spectrumAOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumAOutputSlot), spectrumAUavHandle);
        }

        const int spectrumBOutputSlot = evolutionPipeline.GetComputeRootParamIndex("gDisplacementZOutput");
        if (spectrumBOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumBOutputSlot), spectrumBUavHandle);
        }

        const int constantsSlot = evolutionPipeline.GetComputeRootParamIndex("FFTOceanSimulationConstants");
        if (constantsSlot >= 0 && simulationConstantsBuffer) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(constantsSlot),
                simulationConstantsBuffer->GetGPUVirtualAddress());
        }

        const UINT dispatchX = (resolution + 7) / 8;
        const UINT dispatchY = (resolution + 7) / 8;
        cmdList->Dispatch(dispatchX, dispatchY, 1);

        ResourceBarrierHelper::UAV(cmdList, spectrumTextureA[0].Get());
        ResourceBarrierHelper::UAV(cmdList, spectrumTextureB[0].Get());
    }

    void FFTOceanDispatchHelper::DispatchIFFTPass(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* inputResource,
        D3D12_RESOURCE_STATES& inputState,
        CustomShaderPipeline& pipeline,
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrv,
        ID3D12Resource* outputResource,
        D3D12_RESOURCE_STATES& outputState,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUav,
        D3D12_GPU_VIRTUAL_ADDRESS ifftConstantsGpuAddress,
        uint32_t resolution)
    {
        ResourceBarrierHelper::Transition(cmdList, inputResource, inputState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, outputResource, outputState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(pipeline.GetComputePSO());
        cmdList->SetComputeRootSignature(pipeline.GetComputeRootSignature());

        const int inputSlot = pipeline.GetComputeRootParamIndex("gInputSpectrum");
        if (inputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(inputSlot), inputSrv);
        }

        const int outputSlot = pipeline.GetComputeRootParamIndex("gOutputSpectrum");
        if (outputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(outputSlot), outputUav);
        }

        const int constantsSlot = pipeline.GetComputeRootParamIndex("FFTOceanIFFTConstants");
        if (constantsSlot >= 0 && ifftConstantsGpuAddress != 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(constantsSlot),
                ifftConstantsGpuAddress);
        }

        const UINT dispatchX = (resolution + 7) / 8;
        const UINT dispatchY = (resolution + 7) / 8;
        cmdList->Dispatch(dispatchX, dispatchY, 1);
        ResourceBarrierHelper::UAV(cmdList, outputResource);
    }

    void FFTOceanDispatchHelper::DispatchFinalizePass(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* spectrumAResource,
        D3D12_RESOURCE_STATES& spectrumAState,
        D3D12_GPU_DESCRIPTOR_HANDLE spectrumASrv,
        ID3D12Resource* spectrumBResource,
        D3D12_RESOURCE_STATES& spectrumBState,
        D3D12_GPU_DESCRIPTOR_HANDLE spectrumBSrv,
        CustomShaderPipeline& finalizePipeline,
        D3D12_GPU_DESCRIPTOR_HANDLE displacementUavHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE normalUavHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE jacobianUavHandle,
        ID3D12Resource* simulationConstantsBuffer,
        uint32_t resolution)
    {
        ResourceBarrierHelper::Transition(cmdList, spectrumAResource, spectrumAState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, spectrumBResource, spectrumBState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cmdList->SetPipelineState(finalizePipeline.GetComputePSO());
        cmdList->SetComputeRootSignature(finalizePipeline.GetComputeRootSignature());

        const int spectrumASlot = finalizePipeline.GetComputeRootParamIndex("gHeightDisplacementXSpectrum");
        if (spectrumASlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumASlot), spectrumASrv);
        }

        const int spectrumBSlot = finalizePipeline.GetComputeRootParamIndex("gDisplacementZSpectrum");
        if (spectrumBSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumBSlot), spectrumBSrv);
        }

        const int displacementOutputSlot = finalizePipeline.GetComputeRootParamIndex("gDisplacementOutput");
        if (displacementOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(displacementOutputSlot), displacementUavHandle);
        }

        const int normalOutputSlot = finalizePipeline.GetComputeRootParamIndex("gNormalOutput");
        if (normalOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(normalOutputSlot), normalUavHandle);
        }

        const int jacobianOutputSlot = finalizePipeline.GetComputeRootParamIndex("gJacobianOutput");
        if (jacobianOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(jacobianOutputSlot), jacobianUavHandle);
        }

        const int constantsSlot = finalizePipeline.GetComputeRootParamIndex("FFTOceanSimulationConstants");
        if (constantsSlot >= 0 && simulationConstantsBuffer) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(constantsSlot),
                simulationConstantsBuffer->GetGPUVirtualAddress());
        }

        const UINT dispatchX = (resolution + 7) / 8;
        const UINT dispatchY = (resolution + 7) / 8;
        cmdList->Dispatch(dispatchX, dispatchY, 1);
    }
}
