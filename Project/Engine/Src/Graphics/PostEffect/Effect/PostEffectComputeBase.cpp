#include "pch.h"
#include "PostEffectComputeBase.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Math/IntegerMath.h"
#include <cassert>
#include <stdexcept>


namespace CoreEngine
{
    namespace
    {
        /// @brief DispatchScreenPass のスレッドグループの一辺（シェーダー側の kGroupSize と同じ値）
        constexpr uint32_t kScreenPassGroupSize = 8;
    }

    void PostEffectComputeBase::Initialize(GraphicsCore* dxCommon)
    {
        assert(dxCommon);
        graphicsCore_ = dxCommon;
        assert(shaderProgramCache_
            && "SetShaderProgramCache() を Initialize() の前に呼ぶこと（Manager が行う）");

        // コンパイルとリフレクションはキャッシュが担当する
        shaderProgram_ = shaderProgramCache_->GetOrCreateCompute(
            GetComputeShaderPath(), GetEffectName());
        if (!shaderProgram_) {
            throw std::runtime_error(GetEffectName() + ": Failed to compile compute shader");
        }
        reflectionData_ = &shaderProgram_->GetReflection();

        // RootSignature 構築
        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);
        OnConfigureRootSignature(config);

        rootSignatureManager_ = std::make_unique<RootSignatureManager>();
        auto buildResult = rootSignatureManager_->Build(
            graphicsCore_->GetDevice(), *reflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error(
                GetEffectName() + ": Failed to create RootSignature: " + buildResult.errorMessage);
        }

        // Compute PSO 構築
        computePso_ = ComputePipelineUtil::Create(
            graphicsCore_->GetDevice(), rootSignatureManager_->GetRootSignature(),
            shaderProgram_->GetCS(), GetEffectName() + "_CS");
        if (!computePso_) {
            throw std::runtime_error(GetEffectName() + ": Failed to create Compute PSO");
        }

        // 派生クラス固有のリソース生成
        OnCreateResources();
    }

    void PostEffectComputeBase::UpdateScreenSizeConstants(uint32_t width, uint32_t height)
    {
        ScreenSizeConstants constants{};
        constants.screenWidth  = width;
        constants.screenHeight = height;
        screenSizeCbAddress_ = UploadConstants(constants);
    }

    void PostEffectComputeBase::DispatchScreenPass(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        DispatchScreenPassWithAddress(inputSrvHandle, outputUavHandle, width, height, nullptr, 0);
    }

    void PostEffectComputeBase::DispatchScreenPassWithAddress(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height,
        const char* paramsName,
        D3D12_GPU_VIRTUAL_ADDRESS paramsAddress)
    {
        UpdateScreenSizeConstants(width, height);

        auto* cmdList = graphicsCore_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        const int textureIdx = GetRootParamIndex("gTexture");
        const int outputIdx  = GetRootParamIndex("gOutput");
        const int paramsIdx  = paramsName ? GetRootParamIndex(paramsName) : -1;
        const int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (paramsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(paramsIdx, paramsAddress);
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, GetScreenSizeCbAddress());

        const uint32_t groupX = DivRoundUp(width, kScreenPassGroupSize);
        const uint32_t groupY = DivRoundUp(height, kScreenPassGroupSize);
        cmdList->Dispatch(groupX, groupY, 1);
    }
}
