#include "pch.h"
#include "SkyBoxRenderer.h"
#include "Camera/Camera.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Utility/Logger/Logger.h"
#include <cassert>


namespace CoreEngine
{
    void SkyBoxRenderer::Initialize(ID3D12Device* device) {
        RootSignatureConfig config;
        // Sky-View / Transmittance LUT は u(方位: 太陽↔反太陽)・v(天頂↔地面)の両端が
        // 本来連続しない。既定の WRAP だと反対端の値（地面=黒 / 反太陽=暗）がバイリニア
        // 補間で混入し、天頂の黒い穴・太陽を貫く縦線の原因になるため CLAMP を使う。
        config.ConfigureSampler("gLUTSampler", SamplerConfig::LinearClamp());

        const auto shaders = BuildShadersAndRootSignature(device,
            L"Skybox.VS.hlsl", L"SkyAtmosphere.PS.hlsl", "SkyBoxRenderer", config, "SkyBox");

        // SkyBox は背景として最初に描画し、共有 DSV の内容に依存させない。
        // ブレンドは kBlendModeNone しか使わないので None のみ生成する。
        bool result = psoMg_->CreateBuilder()
            .SetDebugName("SkyBox")
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, false, D3D12_COMPARISON_FUNC_LESS_EQUAL)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .Build(device, shaders.vs.Get(), shaders.ps.Get(), rootSignatureMg_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create SkyBox Pipeline State Object");
        }

        pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNone);

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "SkyBoxRenderer: 大気散乱パイプライン初期化完了");
    }

    int SkyBoxRenderer::GetRootParamIndex(const std::string& resourceName) const {
        return FindRootParamIndex(reflectionData_.get(), resourceName);
    }

    void SkyBoxRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
        (void)blendMode; // SkyBoxはブレンドモード変更不要

        BindPassPipeline(cmdList, rootSignatureMg_->GetRootSignature(), pipelineState_);
    }

    void SkyBoxRenderer::EndPass() {
        // 今は空
    }

    void SkyBoxRenderer::SetCamera(const Camera* camera) {
        // SkyBoxでは特にカメラCBVを保持する必要はないため空実装
        (void)camera;
    }
}
