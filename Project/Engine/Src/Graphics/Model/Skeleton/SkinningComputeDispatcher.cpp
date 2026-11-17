#include "pch.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "SkinningComputeDispatcher.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "SkinCluster.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Math/IntegerMath.h"
#include <cassert>
#include <iterator>
#include <stdexcept>

namespace CoreEngine
{
    namespace
    {
        /// @brief Skinning.CS.hlsl の契約
        namespace SkinningBind {
            enum Slot : size_t {
                gSourceVertices, gInfluences, gMatrixPalette, gPrevMatrixPalette,
                gOutputVertices, gPrevPositions, SkinningParams, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "gSourceVertices",    ShaderBindingType::SRV, BindingUsage::Required }, // t0
                { "gInfluences",        ShaderBindingType::SRV, BindingUsage::Required }, // t1
                { "gMatrixPalette",     ShaderBindingType::SRV, BindingUsage::Required }, // t2
                { "gPrevMatrixPalette", ShaderBindingType::SRV, BindingUsage::Required }, // t3
                { "gOutputVertices",    ShaderBindingType::UAV, BindingUsage::Required }, // u0
                { "gPrevPositions",     ShaderBindingType::UAV, BindingUsage::Required }, // u1
                { "SkinningParams",     ShaderBindingType::CBV, BindingUsage::Required }, // b0
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }
    }

    void SkinningComputeDispatcher::Initialize(ID3D12Device* device)
    {
        assert(device);

        shaderCompiler_->Initialize();
        Microsoft::WRL::ComPtr<IDxcBlob> csBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Skinning/Skinning.CS.hlsl", L"cs_6_0");
        assert(csBlob != nullptr);

        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        reflectionData_ = reflectionBuilder_->BuildFromComputeShader(csBlob.Get(), "SkinningComputeDispatcher");

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
            device, rootSignatureMg_->GetRootSignature(), csBlob.Get(), "SkinningCompute");
        if (!computePso_) {
            throw std::runtime_error("Failed to create Skinning Compute PSO");
        }

        bindings_ = BindingTable::Resolve(*reflectionData_, SkinningBind::kDecls, "SkinningComputeDispatcher");
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
        {
            BarrierBatch toUav(cmdList);
            toUav.Transition(skinCluster.outputVertexResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            toUav.Transition(skinCluster.prevPositionResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        cmdList->SetComputeRootSignature(rootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(bindings_[SkinningBind::gSourceVertices], skinCluster.sourceVertexSrvHandle.Gpu());
        binder.Set(bindings_[SkinningBind::gInfluences], skinCluster.influenceSrvHandle.Gpu());
        binder.Set(bindings_[SkinningBind::gMatrixPalette], skinCluster.palette.Srv());
        binder.Set(bindings_[SkinningBind::gPrevMatrixPalette], skinCluster.prevPalette.Srv());
        binder.Set(bindings_[SkinningBind::gOutputVertices], skinCluster.outputUavHandle.Gpu());
        binder.Set(bindings_[SkinningBind::gPrevPositions], skinCluster.prevPositionUavHandle.Gpu());
        binder.Set(bindings_[SkinningBind::SkinningParams], skinCluster.skinningParamsCB->GetGPUVirtualAddress());
        binder.ValidateBeforeDraw(bindings_);

        const UINT groupCount = DivRoundUp(vertexCount, 64);
        cmdList->Dispatch(groupCount, 1, 1);

        // 出力バッファを頂点バッファとして、前の姿勢の位置を頂点シェーダの SRV として読めるように遷移
        BarrierBatch batch(cmdList);
        batch.UAV(skinCluster.outputVertexResource);
        batch.UAV(skinCluster.prevPositionResource);
        batch.Transition(skinCluster.outputVertexResource,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        batch.Transition(skinCluster.prevPositionResource,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
}
