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
        shaderCompiler_->Initialize();
        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());

        // シェーダーのコンパイル
        auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Skybox.VS.hlsl", L"vs_6_0");
        assert(vertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(L"SkyAtmosphere.PS.hlsl", L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        // リフレクション
        reflectionData_ = reflectionBuilder_->BuildFromShaders(vertexShaderBlob, pixelShaderBlob, "SkyBoxRenderer");

        RootSignatureConfig config = RootSignatureConfig::Simple();
        // Sky-View / Transmittance LUT は u(方位: 太陽↔反太陽)・v(天頂↔地面)の両端が
        // 本来連続しない。既定の WRAP だと反対端の値（地面=黒 / 反太陽=暗）がバイリニア
        // 補間で混入し、天頂の黒い穴・太陽を貫く縦線の原因になるため CLAMP を使う。
        config.ConfigureSampler("gLUTSampler", SamplerConfig::LinearClamp());

        auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);

        if (!buildResult.success) {
            throw std::runtime_error("Failed to create SkyBox Root Signature: " + buildResult.errorMessage);
        }

        // SkyBox は背景として最初に描画し、共有 DSV の内容に依存させない。
        // 複数 View が同一フレームで共有 DSV を使い回すため、深度テストありだと
        // 前後のパスの深度値によって背景だけが不安定に落ちる場合がある。
        // SkyBox は背景描画で kBlendModeNone しか使わないため、
        // 全ブレンドモードではなく None のみ生成する（起動時の無駄なPSO生成を削減）
        bool result = psoMg_->CreateBuilder()
            .SetDebugName("SkyBox")
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, false, D3D12_COMPARISON_FUNC_LESS_EQUAL)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .Build(device, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create SkyBox Pipeline State Object");
        }

        pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNone);

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "SkyBoxRenderer: 大気散乱パイプライン初期化完了");
    }

    int SkyBoxRenderer::GetRootParamIndex(const std::string& resourceName) const {
        if (!reflectionData_) {
            return -1;
        }
        return reflectionData_->GetRootParameterIndexByName(resourceName);
    }

    void SkyBoxRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
        (void)blendMode; // SkyBoxはブレンドモード変更不要

        // RootSignatureを設定
        cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
        // パイプラインステートを設定
        cmdList->SetPipelineState(pipelineState_);
        // 形状を設定
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    void SkyBoxRenderer::EndPass() {
        // 今は空
    }

    void SkyBoxRenderer::SetCamera(const Camera* camera) {
        // SkyBoxでは特にカメラCBVを保持する必要はないため空実装
        (void)camera;
    }
}
