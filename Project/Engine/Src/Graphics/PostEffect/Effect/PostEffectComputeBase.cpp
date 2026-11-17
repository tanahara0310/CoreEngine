#include "pch.h"
#include "PostEffectComputeBase.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
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

        /// @brief テーブルと定数を、findIndex(name) で引いたルート番号へ差す
        /// @details 名前が nullptr のものと、番号が引けない（-1）ものは差さない
        template <class FindIndex, class TableList, class ConstantList>
        void BindNamedArguments(
            ID3D12GraphicsCommandList* cmdList, const FindIndex& findIndex,
            const TableList& tables, const ConstantList& constants)
        {
            for (const auto& table : tables) {
                const int index = table.name ? findIndex(table.name) : -1;
                if (index >= 0) cmdList->SetComputeRootDescriptorTable(index, table.handle);
            }
            for (const auto& constant : constants) {
                const int index = constant.name ? findIndex(constant.name) : -1;
                if (index >= 0) cmdList->SetComputeRootConstantBufferView(index, constant.address);
            }
        }
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
        DispatchPass(graphicsCore_->GetCommandList(),
            { { "gTexture", inputSrvHandle }, { "gOutput", outputUavHandle } },
            { { paramsName, paramsAddress }, { "ScreenParams", GetScreenSizeCbAddress() } },
            width, height);
    }

    void PostEffectComputeBase::BindPass(
        ID3D12GraphicsCommandList* cmdList,
        std::initializer_list<PassTable> tables,
        std::initializer_list<PassConstants> constants)
    {
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());
        BindNamedArguments(cmdList,
            [this](const char* name) { return GetRootParamIndex(name); }, tables, constants);
    }

    void PostEffectComputeBase::BindPass(
        ID3D12GraphicsCommandList* cmdList,
        const CustomShaderPipeline& pipeline,
        std::initializer_list<PassTable> tables,
        std::initializer_list<PassConstants> constants)
    {
        cmdList->SetComputeRootSignature(pipeline.GetComputeRootSignature());
        cmdList->SetPipelineState(pipeline.GetComputePSO());
        BindNamedArguments(cmdList,
            [&pipeline](const char* name) { return pipeline.GetComputeRootParamIndex(name); }, tables, constants);
    }

    void PostEffectComputeBase::DispatchPass(
        ID3D12GraphicsCommandList* cmdList,
        std::initializer_list<PassTable> tables,
        std::initializer_list<PassConstants> constants,
        uint32_t width,
        uint32_t height)
    {
        BindPass(cmdList, tables, constants);
        cmdList->Dispatch(DivRoundUp(width, kScreenPassGroupSize), DivRoundUp(height, kScreenPassGroupSize), 1);
    }

    void PostEffectComputeBase::DispatchPass(
        ID3D12GraphicsCommandList* cmdList,
        const CustomShaderPipeline& pipeline,
        std::initializer_list<PassTable> tables,
        std::initializer_list<PassConstants> constants,
        uint32_t width,
        uint32_t height)
    {
        BindPass(cmdList, pipeline, tables, constants);
        cmdList->Dispatch(DivRoundUp(width, kScreenPassGroupSize), DivRoundUp(height, kScreenPassGroupSize), 1);
    }
}
