#include "pch.h"
#include "ModelRenderer.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Model/TransformationMatrix.h"
#include "Graphics/Material/MaterialConstants.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include <cassert>


namespace CoreEngine
{
    void ModelRenderer::Initialize(ID3D12Device* device) {
        shaderCompiler_->Initialize();

        // フォワードパス用シェーダーをコンパイル
        auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Object/Object3d.VS.hlsl", L"vs_6_0");
        assert(vertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Object/Object3d.PS.hlsl", L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        // GBuffer パス用シェーダーをコンパイル
        auto gBufferVertexShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Object/GBuffer.VS.hlsl", L"vs_6_0");
        assert(gBufferVertexShaderBlob != nullptr);

        auto gBufferPixelShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Object/GBuffer.PS.hlsl", L"ps_6_0");
        assert(gBufferPixelShaderBlob != nullptr);

        // シェーダーリフレクションを構築（RootSignature 自動生成に使用）
        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        forwardReflectionData_ = reflectionBuilder_->BuildFromShaders(vertexShaderBlob.Get(), pixelShaderBlob.Get(), "ModelRenderer");
        gBufferReflectionData_ = reflectionBuilder_->BuildFromShaders(gBufferVertexShaderBlob.Get(), gBufferPixelShaderBlob.Get(), "ModelRenderer_GBuffer");

        // RootSignature 構成: CBV は高速な Root Descriptor、SRV は Descriptor Table
        // ただし gInstanceData のみ Root Descriptor（インスタンシング用バッファは頻繁に変わるため）
        RootSignatureConfig config;
        config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
        config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);
        config.ConfigureResource("gInstanceData", BindingStrategy::RootDescriptor); // インスタンスデータは Root SRV
        config.ConfigureSampler("gShadowSampler", SamplerConfig::Shadow());
        config.ConfigureSampler("gSampler", SamplerConfig::Anisotropic());
        // 頂点アニメーションの時間・風はパスごとに差す 8 個の 32bit 値なのでルート定数にする
        config.ConfigureResource("gVertexAnim", BindingStrategy::RootConstants);

        // フォワードパス用 RootSignature を構築
        auto buildResult = forwardRootSignatureMg_->Build(device, *forwardReflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error("Failed to create Root Signature: " + buildResult.errorMessage);
        }

        // GBuffer パス用 RootSignature を構築
        auto gBufferBuildResult = gBufferRootSignatureMg_->Build(device, *gBufferReflectionData_, config);
        if (!gBufferBuildResult.success) {
            throw std::runtime_error("Failed to create GBuffer Root Signature: " + gBufferBuildResult.errorMessage);
        }

        // CBV サイズ検証: C++ 構造体と HLSL 構造体のレイアウトが一致しているか確認
        forwardReflectionData_->ValidateAllCBVSizes({
            {"gMaterial", sizeof(MaterialConstants)}
            });

        gBufferReflectionData_->ValidateAllCBVSizes({
            {"gMaterial", sizeof(MaterialConstants)}
            });

        // 必須リソースの存在確認は宣言表（ModelBind::kForward / kGBuffer）へ移した。
        // 手書きの 3 個だけでなく 21 個すべてが照合され、種別違いも検出される。
        ResolveBindings(ModelBind::kForward, ModelBind::kGBuffer,
            ModelBind::Slot::Count, "ModelRenderer");

        // フォワードパス PSO: 全ブレンドモード分を事前生成
        bool result = forwardPsoMg_->CreateBuilder()
            .SetDebugName("ModelForward")
            .SetInputLayoutFromReflection(*forwardReflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, vertexShaderBlob.Get(), pixelShaderBlob.Get(), forwardRootSignatureMg_->GetRootSignature());

        // GBuffer パス PSO: マルチレンダーターゲットフォーマットを指定して生成
        bool gBufferResult = gBufferPsoMg_->CreateBuilder()
            .SetDebugName("ModelGBuffer")
            .SetInputLayoutFromReflection(*gBufferReflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .SetRenderTargetFormats(GBufferManager::kRenderTargetFormats, static_cast<UINT>(std::size(GBufferManager::kRenderTargetFormats)))
            .BuildGBuffer(device, gBufferVertexShaderBlob.Get(), gBufferPixelShaderBlob.Get(), gBufferRootSignatureMg_->GetRootSignature());

        if (!result || !gBufferResult) {
            throw std::runtime_error("Failed to create Pipeline State Object");
        }

        // 初期状態として BlendModeNone の PSO を保持
        forwardPipelineState_ = forwardPsoMg_->GetPipelineState(BlendMode::kBlendModeNone);
        gBufferPipelineState_ = gBufferPsoMg_->GetPipelineState(BlendMode::kBlendModeNone);

    }
}
