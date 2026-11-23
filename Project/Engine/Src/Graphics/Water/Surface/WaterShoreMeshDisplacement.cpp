#include "pch.h"
#include "WaterShoreMeshDisplacement.h"

#include <cmath>
#include <exception>
#include <iterator>

#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UavTexture.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/ShaderProgram.h"
#include "Graphics/Water/RayTracing/WaterRayTracingPassBase.h"
#include "Graphics/Water/WaterSurfaceData.h"
#include "Math/IntegerMath.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        namespace DisplacementBind {
            enum Slot : size_t {
                WaterSurfaceData, gShoreMeshDisplacementSource, gShoreMeshDisplacementOutput, gShoreMeshSurfaceOutput,
                Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "WaterSurfaceData",               ShaderBindingType::CBV, BindingUsage::Required },
                { "gShoreMeshDisplacementSource",   ShaderBindingType::SRV, BindingUsage::Required },
                { "gShoreMeshDisplacementOutput",   ShaderBindingType::UAV, BindingUsage::Required },
                { "gShoreMeshSurfaceOutput",        ShaderBindingType::UAV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }

        namespace CoarseBind {
            enum Slot : size_t {
                WaterSurfaceData, gCoarseMeshDisplacementSource, gCoarseMeshDisplacementOutput, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "WaterSurfaceData",                ShaderBindingType::CBV, BindingUsage::Required },
                { "gCoarseMeshDisplacementSource",   ShaderBindingType::SRV, BindingUsage::Required },
                { "gCoarseMeshDisplacementOutput",   ShaderBindingType::UAV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }

        constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
        constexpr uint32_t kThreadGroupSize = 8;

        /// @brief 計算シェーダーのルートシグネチャ・結線表・パイプラインを作る（海の範囲マップなどをヒープから番号で引く cs_6_6）
        template <size_t N>
        bool CreateComputePipeline(
            GraphicsCore* dxCommon, ShaderProgramCache* shaderProgramCache,
            const wchar_t* path, const char* name, const ShaderBindingDecl (&decls)[N],
            RootSignatureManager& rootSignature, BindingTable& bindings,
            Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState)
        {
            Logger& log = Logger::GetInstance();
            const ShaderProgram* program = shaderProgramCache->GetOrCreateCompute(path, name, L"cs_6_6");
            if (!program) {
                log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                    "{}: シェーダーをコンパイルできませんでした", name);
                return false;
            }

            RootSignatureConfig config;
            config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
            const auto buildResult = rootSignature.Build(dxCommon->GetDevice(), program->GetReflection(), config);
            if (!buildResult.success) {
                log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                    "{}: ルートシグネチャを作れませんでした: {}", name, buildResult.errorMessage);
                return false;
            }

            try {
                bindings = BindingTable::Resolve(program->GetReflection(), decls, name);
            }
            catch (const std::exception& e) {
                log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                    "{}: シェーダーの結線が宣言と合いません: {}", name, e.what());
                return false;
            }

            pipelineState = ComputePipelineUtil::Create(
                dxCommon->GetDevice(), rootSignature.GetRootSignature(), program->GetCS(), name);
            return pipelineState != nullptr;
        }
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
        isInitialized_ = CreatePipelines() && CreateTextures();
        return isInitialized_;
    }

    bool WaterShoreMeshDisplacement::CreatePipelines()
    {
        return CreateComputePipeline(dxCommon_, shaderProgramCache_,
                L"Engine/Assets/Shaders/Water/Simulation/WaterShoreMeshDisplacement.CS.hlsl",
                "WaterShoreMeshDisplacement", DisplacementBind::kDecls,
                rootSignature_, bindings_, pipelineState_)
            && CreateComputePipeline(dxCommon_, shaderProgramCache_,
                L"Engine/Assets/Shaders/Water/Simulation/WaterCoarseMeshDisplacement.CS.hlsl",
                "WaterCoarseMeshDisplacement", CoarseBind::kDecls,
                coarseRootSignature_, coarseBindings_, coarsePipelineState_);
    }

    bool WaterShoreMeshDisplacement::CreateTextures()
    {
        const D3D12_RESOURCE_DESC desc = UavTexture::MakeDesc(kVertexCount, kVertexCount, 1, kFormat);
        if (!UavTexture::CreateResource(
                dxCommon_->GetDevice(), texture_, desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
            || !UavTexture::CreateResource(
                dxCommon_->GetDevice(), surfaceTexture_, desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) {
            return false;
        }
        UavTexture::EnsureViews(*descriptorAllocator_, texture_.Get(), desc,
            srv_, "WaterShoreMeshDisplacementSRV", uav_, "WaterShoreMeshDisplacementUAV");
        UavTexture::EnsureViews(*descriptorAllocator_, surfaceTexture_.Get(), desc,
            surfaceSrv_, "WaterShoreMeshSurfaceSRV", surfaceUav_, "WaterShoreMeshSurfaceUAV");
        return true;
    }

    bool WaterShoreMeshDisplacement::EnsureCoarseMesh(uint32_t subdivisions)
    {
        if (!isInitialized_ || subdivisions == 0) {
            return false;
        }
        const uint32_t vertexCount = subdivisions + 1;
        if (vertexCount == coarseVertexCount_) {
            return true;
        }

        // 前のテクスチャを読み書きしている GPU の処理が終わってから作り直す
        if (coarseTexture_.IsValid()) {
            dxCommon_->WaitForGpuIdle();
        }
        coarseVertexCount_ = 0;
        const D3D12_RESOURCE_DESC desc = UavTexture::MakeDesc(vertexCount, vertexCount, 1, kFormat);
        if (!UavTexture::CreateResource(
                dxCommon_->GetDevice(), coarseTexture_, desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterShoreMeshDisplacement: 粗い格子の変位のテクスチャ（{0}x{0}）を作れませんでした", vertexCount);
            return false;
        }
        UavTexture::EnsureViews(*descriptorAllocator_, coarseTexture_.Get(), desc,
            coarseSrv_, "WaterCoarseMeshDisplacementSRV", coarseUav_, "WaterCoarseMeshDisplacementUAV");
        coarseVertexCount_ = vertexCount;
        return true;
    }

    void WaterShoreMeshDisplacement::Dispatch(
        ID3D12GraphicsCommandList* cmdList, const WaterSurfaceData& surface, uint64_t frameNumber)
    {
        if (!isInitialized_ || !cmdList || surface.fftOcean.displacementSRV.ptr == 0) {
            return;
        }
        if (frameNumber == lastFrameNumber_) {
            return;
        }
        lastFrameNumber_ = frameNumber;

        const D3D12_GPU_VIRTUAL_ADDRESS surfaceConstants = dxCommon_->GetUploadRing().AllocateConstants(
            WaterRayTracingPassBase::BuildSurfaceConstants(surface, surface.fftOcean));

        // 粗い格子（岸の格子の範囲の端が読むので先に書く）
        const bool writeCoarse = surface.regionValid != 0 && coarseVertexCount_ != 0
            && surface.coarseMeshVertexCount == coarseVertexCount_
            && coarseVertexCount_ == static_cast<uint32_t>(std::lround(surface.meshSubdivisions)) + 1;
        if (writeCoarse) {
            Barrier::Transition(cmdList, coarseTexture_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cmdList->SetPipelineState(coarsePipelineState_.Get());
            cmdList->SetComputeRootSignature(coarseRootSignature_.GetRootSignature());
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(coarseBindings_[CoarseBind::WaterSurfaceData], surfaceConstants);
            binder.Set(coarseBindings_[CoarseBind::gCoarseMeshDisplacementSource], surface.fftOcean.displacementSRV);
            binder.Set(coarseBindings_[CoarseBind::gCoarseMeshDisplacementOutput], coarseUav_.gpuHandle);
            binder.ValidateBeforeDraw(coarseBindings_);

            const UINT groups = DivRoundUp(coarseVertexCount_, kThreadGroupSize);
            cmdList->Dispatch(groups, groups, 1);
            Barrier::Transition(cmdList, coarseTexture_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        if (surface.shoreMeshEnabled == 0) {
            return;
        }
        {
            BarrierBatch toWrite(cmdList);
            toWrite.Transition(texture_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            toWrite.Transition(surfaceTexture_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        cmdList->SetPipelineState(pipelineState_.Get());
        cmdList->SetComputeRootSignature(rootSignature_.GetRootSignature());
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(bindings_[DisplacementBind::WaterSurfaceData], surfaceConstants);
        binder.Set(bindings_[DisplacementBind::gShoreMeshDisplacementSource], surface.fftOcean.displacementSRV);
        binder.Set(bindings_[DisplacementBind::gShoreMeshDisplacementOutput], uav_.gpuHandle);
        binder.Set(bindings_[DisplacementBind::gShoreMeshSurfaceOutput], surfaceUav_.gpuHandle);
        binder.ValidateBeforeDraw(bindings_);

        const UINT groups = DivRoundUp(kVertexCount, kThreadGroupSize);
        cmdList->Dispatch(groups, groups, 1);
        BarrierBatch toRead(cmdList);
        toRead.Transition(texture_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        toRead.Transition(surfaceTexture_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
}
