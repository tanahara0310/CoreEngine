#include "SkinnedModelRenderer.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Model/TransformationMatrix.h"
#include "Graphics/Material/MaterialConstants.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include <cassert>
#include <cstring>


namespace CoreEngine
{
    void SkinnedModelRenderer::Initialize(ID3D12Device* device) {
        shaderCompiler_->Initialize();

        // スキニング専用フォワードパス用シェーダーをコンパイル（ボーン影響 Slot1 に対応した VS）
        auto skinningVertexShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Skinning/SkinningObject3d.VS.hlsl", L"vs_6_0");
        assert(skinningVertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Object/Object3d.PS.hlsl", L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        // スキニング専用 GBuffer パス用シェーダーをコンパイル
        auto gBufferVertexShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Skinning/GBufferSkinning.VS.hlsl", L"vs_6_0");
        assert(gBufferVertexShaderBlob != nullptr);

        auto gBufferPixelShaderBlob = shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Object/GBuffer.PS.hlsl", L"ps_6_0");
        assert(gBufferPixelShaderBlob != nullptr);

        // シェーダーリフレクションを構築（RootSignature 自動生成に使用）
        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        forwardReflectionData_ = reflectionBuilder_->BuildFromShaders(skinningVertexShaderBlob, pixelShaderBlob, "SkinnedModelRenderer");
        gBufferReflectionData_ = reflectionBuilder_->BuildFromShaders(gBufferVertexShaderBlob, gBufferPixelShaderBlob, "SkinnedModelRenderer_GBuffer");

        // RootSignature 構成: CBV は高速な Root Descriptor、SRV は Descriptor Table
        RootSignatureConfig config = RootSignatureConfig::PerformanceOptimized();
        config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
        config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);
        config.ConfigureSampler("gShadowSampler", SamplerConfig::Shadow());
        config.ConfigureSampler("gSampler", SamplerConfig::Anisotropic());

        // フォワードパス用 RootSignature を構築
        auto buildResult = forwardRootSignatureMg_->Build(device, *forwardReflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error("Failed to create Skinned Root Signature: " + buildResult.errorMessage);
        }

        // GBuffer パス用 RootSignature を構築
        auto gBufferBuildResult = gBufferRootSignatureMg_->Build(device, *gBufferReflectionData_, config);
        if (!gBufferBuildResult.success) {
            throw std::runtime_error("Failed to create Skinned GBuffer Root Signature: " + gBufferBuildResult.errorMessage);
        }

        // CBV サイズ検証: C++ 構造体と HLSL 構造体のレイアウトが一致しているか確認
        forwardReflectionData_->ValidateAllCBVSizes({
            {"gTransformationMatrix", sizeof(TransformationMatrix)},
            {"gMaterial", sizeof(MaterialConstants)},
            {"gIBLParams", sizeof(IBLSceneParamsCPU)}
            });

        gBufferReflectionData_->ValidateAllCBVSizes({
            {"gTransformationMatrix", sizeof(TransformationMatrix)},
            {"gMaterial", sizeof(MaterialConstants)}
            });

        // フォワードパス PSO: WEIGHT/INDEX は自動検出でスロット 1 に割り当て、全ブレンドモード分を生成
        bool skinningResult = forwardPsoMg_->CreateBuilder()
            .SetInputLayoutFromReflection(*forwardReflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, skinningVertexShaderBlob, pixelShaderBlob, forwardRootSignatureMg_->GetRootSignature());

        // GBuffer パス PSO: マルチレンダーターゲットフォーマットを指定して生成
        bool gBufferResult = gBufferPsoMg_->CreateBuilder()
            .SetInputLayoutFromReflection(*gBufferReflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .SetRenderTargetFormats(GBufferManager::kRenderTargetFormats, static_cast<UINT>(std::size(GBufferManager::kRenderTargetFormats)))
            .BuildGBuffer(device, gBufferVertexShaderBlob, gBufferPixelShaderBlob, gBufferRootSignatureMg_->GetRootSignature());

        if (!skinningResult || !gBufferResult) {
            throw std::runtime_error("Failed to create Skinning Pipeline State Object");
        }

        // 初期状態として BlendModeNone の PSO を保持
        forwardPipelineState_ = forwardPsoMg_->GetPipelineState(BlendMode::kBlendModeNone);
        gBufferPipelineState_ = gBufferPsoMg_->GetPipelineState(BlendMode::kBlendModeNone);

        // IBL 回転パラメータ用の定数バッファを確保し、デフォルト値（回転なし）で初期化
        iblParamsBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(IBLSceneParamsCPU));
        iblParamsCBVAddress_ = iblParamsBuffer_->GetGPUVirtualAddress();
        IBLSceneParamsCPU defaults{ 0.0f, 0.0f, 0.0f, 0.0f };
        void* mapped = nullptr;
        iblParamsBuffer_->Map(0, nullptr, &mapped);
        std::memcpy(mapped, &defaults, sizeof(defaults));
        iblParamsBuffer_->Unmap(0, nullptr);
    }
}
