#include "pch.h"
#include "SpriteRenderer.h"
#include "Camera/Camera.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Render/RenderTarget/SceneDepth.h"
#include <cassert>


namespace CoreEngine
{
    void SpriteRenderer::InitializePipeline(ID3D12Device* device) {
        // 線形補間とドット絵向けの 2 つを持ち、どちらで読むかはマテリアルが決める
        RootSignatureConfig config;
        config.ConfigureSampler("gSampler", SamplerConfig::Linear());
        config.ConfigureSampler("gPointSampler", SamplerConfig::Point());

        const auto shaders = BuildShadersAndRootSignature(device,
            L"Engine/Assets/Shaders/Sprite/Sprite.VS.hlsl", L"Engine/Assets/Shaders/Sprite/Sprite.PS.hlsl",
            "SpriteRenderer", config, "Sprite");

        bool result = psoMg_->CreateBuilder()
            .SetDebugName("Sprite")
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, false)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, shaders.vs.Get(), shaders.ps.Get(), rootSignatureMg_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create Sprite Pipeline State Object");
        }

        pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNormal);
        currentBlendMode_ = BlendMode::kBlendModeNormal;
    }

    void SpriteRenderer::Initialize(ID3D12Device* /*device*/) {
        assert(false && "SpriteRenderer: Use Initialize(GraphicsCore*) instead of Initialize(ID3D12Device*).");
    }

    int SpriteRenderer::GetRootParamIndex(const std::string& resourceName) const {
        return FindRootParamIndex(reflectionData_.get(), resourceName);
    }

    void SpriteRenderer::Initialize(GraphicsCore* dxCommon) {
        dxCommon_ = dxCommon;

        // パイプラインをデバイスで初期化
        InitializePipeline(dxCommon->GetDevice());

        quadIndexResource_ = ResourceFactory::CreateQuadIndexBuffer(dxCommon->GetDevice(), quadIndexBufferView_);
    }

    void SpriteRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
        BeginBlendModePass(cmdList, blendMode);
        boundPipeline_ = nullptr;
    }

    const ShaderMaterialPipeline* SpriteRenderer::GetMaterialPipeline(const ShaderMaterial& material) {
        if (!dxCommon_) {
            return nullptr;
        }
        return materialPipelines_.Get(*dxCommon_, *shaderCompiler_, *reflectionBuilder_, material);
    }

    void SpriteRenderer::BindPipeline(ID3D12GraphicsCommandList* cmdList, const ShaderMaterialPipeline* pipeline) {
        if (pipeline == boundPipeline_) {
            return;
        }
        if (pipeline) {
            cmdList->SetGraphicsRootSignature(pipeline->GetRootSignature());
            cmdList->SetPipelineState(pipeline->GetPipelineState(currentBlendMode_));
            // 画面の深度は半透明の前に写したもの（写しが無いフレームは「いちばん遠い」）
            if (sceneDepth_) {
                if (pipeline->GetSceneDepthSlot().IsValid()) {
                    cmdList->SetGraphicsRootDescriptorTable(pipeline->GetSceneDepthSlot().index,
                        sceneDepth_->AcquireTransparentReadSRV());
                }
                if (pipeline->GetSceneDepthParamsSlot().IsValid() && sceneDepth_->GetShaderParams() != 0) {
                    cmdList->SetGraphicsRootConstantBufferView(pipeline->GetSceneDepthParamsSlot().index,
                        sceneDepth_->GetShaderParams());
                }
            }
        } else {
            cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
            cmdList->SetPipelineState(pipelineState_);
        }
        boundPipeline_ = pipeline;
    }

    void SpriteRenderer::EndPass() {
        // 今は空
    }

    void SpriteRenderer::SetCamera(const Camera* camera) {
        // 行列は描くたびにビューから取る
        (void)camera;
    }
}
