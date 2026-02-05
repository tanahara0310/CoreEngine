#include "SkyBoxRenderer.h"
#include "Engine/Camera/ICamera.h"
#include <cassert>


namespace CoreEngine
{
void SkyBoxRenderer::Initialize(ID3D12Device* device) {
    // ★既存のSkyBoxRender::Initialize()の実装をコピー
    
    shaderCompiler_->Initialize();
    
    // ===== SkyBox用RootSignatureの初期化 =====
    
    // Root Parameter 0: トランスフォーム用CBV (b0, Vertex Shader)
    RootSignatureManager::RootDescriptorConfig transformCBV;
    transformCBV.shaderRegister = 0;
    transformCBV.visibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootSignatureMg_->AddRootCBV(transformCBV);
    
    // Root Parameter 1: マテリアル用CBV (b0, Pixel Shader)
    RootSignatureManager::RootDescriptorConfig materialCBV;
    materialCBV.shaderRegister = 0;
    materialCBV.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootSignatureMg_->AddRootCBV(materialCBV);
    
    // Root Parameter 2: テクスチャ用ディスクリプタテーブル (t0, Pixel Shader)
    RootSignatureManager::DescriptorRangeConfig textureRange;
    textureRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.numDescriptors = 1;
    textureRange.baseShaderRegister = 0;  // t0
    rootSignatureMg_->AddDescriptorTable({ textureRange }, D3D12_SHADER_VISIBILITY_PIXEL);
    
    // Static Sampler (s0, Pixel Shader)
    rootSignatureMg_->AddDefaultLinearSampler(0, D3D12_SHADER_VISIBILITY_PIXEL);
    
    rootSignatureMg_->Create(device); // ルートシグネチャの作成
    
    // ===== SkyBox用シェーダーのコンパイルとPSO作成 =====
    auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Skybox/Skybox.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);
    
    auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Skybox/Skybox.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);
    
    // SkyBoxは裏面を描画するため、カリングモードをFRONTに設定
    // また、深度テストはLESS_EQUALを使用して最遠方に描画
    bool result = psoMg_->CreateBuilder()
        .AddInputElement("POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_APPEND_ALIGNED_ELEMENT)
        .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
        .SetDepthStencil(true, false, D3D12_COMPARISON_FUNC_LESS_EQUAL) // 深度書き込み無効、LESS_EQUAL比較
        .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
        .BuildAllBlendModes(device, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());
    
    if (!result) {
        throw std::runtime_error("Failed to create SkyBox Pipeline State Object");
    }
    
    pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNone);
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
