#include "pch.h"
#include "WaterShoreMeshDisplacement.h"

#include <exception>
#include <iterator>

#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/ShaderProgram.h"
#include "Graphics/Water/RayTracing/WaterRayTracingPassBase.h"
#include "Graphics/Water/WaterSurfaceData.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        namespace DisplacementBind {
            enum Slot : size_t {
                WaterSurfaceData, gShoreMeshDisplacementSource, gShoreMeshDisplacementOutput, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "WaterSurfaceData",               ShaderBindingType::CBV, BindingUsage::Required },
                { "gShoreMeshDisplacementSource",   ShaderBindingType::SRV, BindingUsage::Required },
                { "gShoreMeshDisplacementOutput",   ShaderBindingType::UAV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }

        constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
        constexpr uint32_t kThreadGroupSize = 8;
    }

    bool WaterShoreMeshDisplacement::Initialize(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        ShaderProgramCache* shaderProgramCache)
    {
        dxCommon_ = dxCommon;
        descriptorAllocator_ = descriptorAllocator;
        shaderProgramCache_ = shaderProgramCache;
        if (!dxCommon_ || !descriptorAllocator_ || !shaderProgramCache_) {
            return false;
        }
        isInitialized_ = CreatePipeline() && CreateTexture();
        return isInitialized_;
    }

    bool WaterShoreMeshDisplacement::CreatePipeline()
    {
        Logger& log = Logger::GetInstance();
        // 海の範囲マップをヒープから番号で引く（cs_6_6）
        const ShaderProgram* program = shaderProgramCache_->GetOrCreateCompute(
            L"Engine/Assets/Shaders/Water/Simulation/WaterShoreMeshDisplacement.CS.hlsl",
            "WaterShoreMeshDisplacement", L"cs_6_6");
        if (!program) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterShoreMeshDisplacement: 岸の格子の変位のシェーダーをコンパイルできませんでした");
            return false;
        }

        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
        const auto buildResult = rootSignature_.Build(dxCommon_->GetDevice(), program->GetReflection(), config);
        if (!buildResult.success) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterShoreMeshDisplacement: ルートシグネチャを作れませんでした: {}", buildResult.errorMessage);
            return false;
        }

        try {
            bindings_ = BindingTable::Resolve(
                program->GetReflection(), DisplacementBind::kDecls, "WaterShoreMeshDisplacement");
        }
        catch (const std::exception& e) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterShoreMeshDisplacement: シェーダーの結線が宣言と合いません: {}", e.what());
            return false;
        }

        pipelineState_ = ComputePipelineUtil::Create(
            dxCommon_->GetDevice(), rootSignature_.GetRootSignature(), program->GetCS(), "WaterShoreMeshDisplacement");
        return pipelineState_ != nullptr;
    }

    bool WaterShoreMeshDisplacement::CreateTexture()
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kVertexCount;
        desc.Height = kVertexCount;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = kFormat;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        try {
            texture_.Reset(
                ResourceFactory::CreateTextureResource(
                    dxCommon_->GetDevice(), desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        catch (const std::exception&) {
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = kFormat;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        srv_ = descriptorAllocator_->CreateSRV(texture_.Get(), srvDesc, "WaterShoreMeshDisplacementSRV");

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = kFormat;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav_ = descriptorAllocator_->CreateUAV(texture_.Get(), uavDesc, "WaterShoreMeshDisplacementUAV");
        return srv_.IsValid() && uav_.IsValid();
    }

    void WaterShoreMeshDisplacement::Dispatch(
        ID3D12GraphicsCommandList* cmdList, const WaterSurfaceData& surface, uint64_t frameNumber)
    {
        if (!isInitialized_ || !cmdList || surface.shoreMeshEnabled == 0
            || surface.fftOcean.displacementSRV.ptr == 0) {
            return;
        }
        if (frameNumber == lastFrameNumber_) {
            return;
        }
        lastFrameNumber_ = frameNumber;

        const D3D12_GPU_VIRTUAL_ADDRESS surfaceConstants = dxCommon_->GetUploadRing().AllocateConstants(
            WaterRayTracingPassBase::BuildSurfaceConstants(surface, surface.fftOcean));

        Barrier::Transition(cmdList, texture_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->SetPipelineState(pipelineState_.Get());
        cmdList->SetComputeRootSignature(rootSignature_.GetRootSignature());
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(bindings_[DisplacementBind::WaterSurfaceData], surfaceConstants);
        binder.Set(bindings_[DisplacementBind::gShoreMeshDisplacementSource], surface.fftOcean.displacementSRV);
        binder.Set(bindings_[DisplacementBind::gShoreMeshDisplacementOutput], uav_.gpuHandle);
        binder.ValidateBeforeDraw(bindings_);

        const UINT groups = (kVertexCount + kThreadGroupSize - 1) / kThreadGroupSize;
        cmdList->Dispatch(groups, groups, 1);
        Barrier::Transition(cmdList, texture_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
}
