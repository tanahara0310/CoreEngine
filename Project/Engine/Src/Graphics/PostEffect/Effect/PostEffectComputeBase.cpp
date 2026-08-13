#include "pch.h"
#include "PostEffectComputeBase.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Resource/ResourceFactory.h"
#include <cassert>
#include <stdexcept>


namespace CoreEngine
{
    void PostEffectComputeBase::Initialize(DirectXCommon* dxCommon)
    {
        assert(dxCommon);
        directXCommon_ = dxCommon;

        //CS シェーダーをコンパイル
        ShaderCompiler compiler;
        compiler.Initialize();
        computeShaderBlob_ = compiler.CompileShader(GetComputeShaderPath(), L"cs_6_0");

        //リフレクション
        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(compiler.GetDxcUtils());
        reflectionData_ = reflectionBuilder.BuildFromComputeShader(
            computeShaderBlob_.Get(), GetEffectName());

        // RootSignature 構築
        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);
        OnConfigureRootSignature(config);

        rootSignatureManager_ = std::make_unique<RootSignatureManager>();
        auto buildResult = rootSignatureManager_->Build(
            directXCommon_->GetDevice(), *reflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error(
                GetEffectName() + ": Failed to create RootSignature: " + buildResult.errorMessage);
        }

        // Compute PSO 構築
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = rootSignatureManager_->GetRootSignature();
        desc.CS = { computeShaderBlob_->GetBufferPointer(), computeShaderBlob_->GetBufferSize() };

        HRESULT hr = directXCommon_->GetDevice()->CreateComputePipelineState(
            &desc, IID_PPV_ARGS(&computePso_));
        if (FAILED(hr)) {
            throw std::runtime_error(GetEffectName() + ": Failed to create Compute PSO");
        }

        // 画面サイズ定数バッファは全 CS エフェクト共通なので基底が用意する。
        // 派生の OnCreateConstantBuffers より先に作ること（派生が中で使うため）
        {
            const UINT size = (sizeof(ScreenSizeConstants) + 255) & ~255u;
            screenSizeCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), size);
            HRESULT mapResult = screenSizeCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenSize_));
            if (FAILED(mapResult)) {
                throw std::runtime_error(GetEffectName() + ": Failed to map ScreenSize constant buffer");
            }
        }

        // 派生クラスの定数バッファ生成
        OnCreateConstantBuffers();
    }

    void PostEffectComputeBase::UpdateScreenSizeConstants(uint32_t width, uint32_t height)
    {
        if (mappedScreenSize_) {
            mappedScreenSize_->screenWidth  = width;
            mappedScreenSize_->screenHeight = height;
        }
    }

    D3D12_GPU_VIRTUAL_ADDRESS PostEffectComputeBase::GetScreenSizeCbAddress() const
    {
        return screenSizeCB_ ? screenSizeCB_->GetGPUVirtualAddress() : 0;
    }
}
