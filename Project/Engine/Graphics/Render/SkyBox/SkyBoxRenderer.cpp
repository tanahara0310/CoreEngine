#include "SkyBoxRenderer.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Graphics/Shader/ShaderReflectionData.h"
#include "Engine/Graphics/RootSignature/RootSignatureConfig.h"
#include "Engine/Utility/Logger/Logger.h"
#include <cassert>


namespace CoreEngine
{
void SkyBoxRenderer::Initialize(ID3D12Device* device) {
    shaderCompiler_->Initialize();
    reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
    
    // シェーダーのコンパイル
    auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Skybox/Skybox.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);
    
    auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Skybox/Skybox.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);
    
    // リフレクション
    reflectionData_ = reflectionBuilder_->BuildFromShaders(vertexShaderBlob, pixelShaderBlob, "SkyBoxRenderer");
    
    // シンプルな設定でRootSignatureを構築
    RootSignatureConfig config = RootSignatureConfig::Simple();
    config.ConfigureSampler("gSampler", SamplerConfig::Linear());
    
    auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);
    
    if (!buildResult.success) {
        throw std::runtime_error("Failed to create SkyBox Root Signature: " + buildResult.errorMessage);
    }
    
    // SkyBoxは裏面を描画するため、カリングモードをFRONTに設定
    // また、深度テストはLESS_EQUALを使用して最遠方に描画
    bool result = psoMg_->CreateBuilder()
        .SetInputLayoutFromReflection(*reflectionData_)
        .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
        .SetDepthStencil(true, false, D3D12_COMPARISON_FUNC_LESS_EQUAL) // 深度書き込み無効、LESS_EQUAL比較
        .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
        .BuildAllBlendModes(device, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());
    
    if (!result) {
        throw std::runtime_error("Failed to create SkyBox Pipeline State Object");
    }
    
    pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNone);
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

void SkyBoxRenderer::SetCamera(const ICamera* camera) {
    // SkyBoxでは特にカメラCBVを保持する必要はないため空実装
    (void)camera;
}
}
