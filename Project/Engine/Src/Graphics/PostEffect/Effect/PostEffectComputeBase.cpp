#include "pch.h"
#include "PostEffectComputeBase.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include <cassert>
#include <stdexcept>


namespace CoreEngine
{
    void PostEffectComputeBase::Initialize(GraphicsCore* dxCommon)
    {
        assert(dxCommon);
        graphicsCore_ = dxCommon;

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
            graphicsCore_->GetDevice(), *reflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error(
                GetEffectName() + ": Failed to create RootSignature: " + buildResult.errorMessage);
        }

        // Compute PSO 構築
        computePso_ = ComputePipelineUtil::Create(
            graphicsCore_->GetDevice(), rootSignatureManager_->GetRootSignature(),
            computeShaderBlob_.Get(), GetEffectName() + "_CS");
        if (!computePso_) {
            throw std::runtime_error(GetEffectName() + ": Failed to create Compute PSO");
        }

        // 画面サイズ定数は全 CS エフェクト共通だが、実体は毎フレーム UploadRing から取る。
        // （専用バッファを 1 本持って毎フレーム上書きすると、GPU が前フレームの
        //   ディスパッチを実行する前に CPU が書き潰す）

        // 派生クラスの定数バッファ生成
        OnCreateConstantBuffers();
    }

    void PostEffectComputeBase::UpdateScreenSizeConstants(uint32_t width, uint32_t height)
    {
        ScreenSizeConstants constants{};
        constants.screenWidth  = width;
        constants.screenHeight = height;
        screenSizeCbAddress_ = graphicsCore_->GetUploadRing().AllocateConstants(constants);
    }
}
