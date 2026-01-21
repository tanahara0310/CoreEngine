#include "Dissolve.h"
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Graphics/RootSignatureManager.h"
#include <cassert>

namespace CoreEngine
{
void Dissolve::Initialize(DirectXCommon* dxCommon)
{
    assert(dxCommon);
    directXCommon_ = dxCommon;

    // シェーダーのコンパイル
    ShaderCompiler shaderCompiler;
    shaderCompiler.Initialize();

    fullscreenVertexShaderBlob_ = shaderCompiler.CompileShader(
        L"Assets/Shaders/PostProcess/FullScreen.VS.hlsl", L"vs_6_0");
    pixelShaderBlob_ = shaderCompiler.CompileShader(
        GetPixelShaderPath(), L"ps_6_0");

    // カスタムルートシグネチャの作成
    CreateCustomRootSignature();

    // パイプラインステートの作成
    bool result = pipelineStateManager_.CreateBuilder()
        .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
        .SetDepthStencil(false, false)
        .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
        .Build(dxCommon->GetDevice(), fullscreenVertexShaderBlob_.Get(), pixelShaderBlob_.Get(), rootSignature_.Get());

    if (!result) {
        throw std::runtime_error("Failed to create PSO in Dissolve");
    }

    // 定数バッファの作成
    CreateConstantBuffer();
    
    // ノイズテクスチャの読み込み
    LoadNoiseTexture();
}

void Dissolve::CreateCustomRootSignature()
{
    RootSignatureManager rootSignatureManager;
    
    // Root Parameter 0: 入力テクスチャ用ディスクリプタテーブル (t0, Pixel Shader)
    RootSignatureManager::DescriptorRangeConfig inputTextureRange;
    inputTextureRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    inputTextureRange.numDescriptors = 1;
    inputTextureRange.baseShaderRegister = 0;  // t0
    rootSignatureManager.AddDescriptorTable({ inputTextureRange }, D3D12_SHADER_VISIBILITY_PIXEL);
    
    // Root Parameter 1: 定数バッファ用CBV (b0, Pixel Shader)
    RootSignatureManager::RootDescriptorConfig paramsCBV;
    paramsCBV.shaderRegister = 0;
    paramsCBV.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootSignatureManager.AddRootCBV(paramsCBV);

    // Root Parameter 2: ノイズテクスチャ用ディスクリプタテーブル (t1, Pixel Shader)
    RootSignatureManager::DescriptorRangeConfig noiseTextureRange;
    noiseTextureRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    noiseTextureRange.numDescriptors = 1;
    noiseTextureRange.baseShaderRegister = 1;  // t1
    rootSignatureManager.AddDescriptorTable({ noiseTextureRange }, D3D12_SHADER_VISIBILITY_PIXEL);

    // Static Sampler (s0, Pixel Shader)
    RootSignatureManager::StaticSamplerConfig samplerConfig;
    samplerConfig.shaderRegister = 0;
    samplerConfig.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplerConfig.filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerConfig.addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerConfig.addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerConfig.addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerConfig.comparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerConfig.maxLOD = D3D12_FLOAT32_MAX;
    rootSignatureManager.AddStaticSampler(samplerConfig);

    rootSignatureManager.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    rootSignatureManager.Create(directXCommon_->GetDevice());
    rootSignature_ = rootSignatureManager.GetRootSignature();
}

void Dissolve::Draw(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
{
    auto* commandList = directXCommon_->GetCommandList();

    // デバッグ出力
    OutputDebugStringA("Dissolve::Draw called\n");

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(
        pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // Root Parameter 0: 入力テクスチャ
    commandList->SetGraphicsRootDescriptorTable(0, inputSrvHandle);

    // Root Parameter 1: 定数バッファ
    if (constantBuffer_) {
        commandList->SetGraphicsRootConstantBufferView(1, constantBuffer_->GetGPUVirtualAddress());
    }

    // Root Parameter 2: ノイズテクスチャ
    commandList->SetGraphicsRootDescriptorTable(2, noiseTextureHandle_);
    
    commandList->DrawInstanced(3, 1, 0, 0);
}

void Dissolve::DrawImGui()
{
#ifdef _DEBUG
    ImGui::PushID("DissolveParams");
    
    ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
    ImGui::Text("ノイズテクスチャを使用してディゾルブ効果を作成します");
    ImGui::Separator();
    
    bool paramsChanged = false;
    
    // パラメータ設定
    if (ImGui::TreeNode("パラメータ")) {
        // ディゾルブ閾値の調整
        paramsChanged |= ImGui::SliderFloat("閾値", &params_.threshold, 0.0f, 1.0f);
        
        // エッジ幅の調整
        paramsChanged |= ImGui::SliderFloat("エッジ幅", &params_.edgeWidth, 0.0f, 0.5f);
        
        // エッジカラーの調整
        float edgeColor[3] = { params_.edgeColorR, params_.edgeColorG, params_.edgeColorB };
        if (ImGui::ColorEdit3("エッジカラー", edgeColor)) {
            params_.edgeColorR = edgeColor[0];
            params_.edgeColorG = edgeColor[1];
            params_.edgeColorB = edgeColor[2];
            paramsChanged = true;
        }
        
        ImGui::TreePop();
    }
    
    // パラメータが変更された場合、即座に定数バッファを更新
    if (paramsChanged) {
        UpdateConstantBuffer();
    }
    
    ImGui::Separator();
    
    if (ImGui::Button("デフォルトに戻す")) {
        params_.threshold = 0.0f;
        params_.edgeWidth = 0.1f;
        params_.edgeColorR = 1.0f;
        params_.edgeColorG = 0.5f;
        params_.edgeColorB = 0.0f;
        UpdateConstantBuffer();
    }
    
    if (!IsEnabled()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
    }
    
    ImGui::Separator();
    
    ImGui::PopID();
#endif // _DEBUG
}

void Dissolve::SetParams(const DissolveParams& params)
{
    params_ = params;
    UpdateConstantBuffer();
}

void Dissolve::SetThreshold(float threshold)
{
    params_.threshold = threshold;
    UpdateConstantBuffer();
}

void Dissolve::SetEdgeWidth(float width)
{
    params_.edgeWidth = width;
    UpdateConstantBuffer();
}

void Dissolve::SetEdgeColor(float r, float g, float b)
{
    params_.edgeColorR = r;
    params_.edgeColorG = g;
    params_.edgeColorB = b;
    UpdateConstantBuffer();
}

void Dissolve::ForceUpdateConstantBuffer()
{
    UpdateConstantBuffer();
}

void Dissolve::BindOptionalCBVs(ID3D12GraphicsCommandList* /*commandList*/)
{
    // この関数はDissolveでは使用しない（Drawで直接バインドしているため）
}

void Dissolve::UpdateConstantBuffer()
{
    // 定数バッファにデータをコピー
    if (mappedData_) {
        *mappedData_ = params_;
    }
}

void Dissolve::CreateConstantBuffer()
{
    assert(directXCommon_);
    
    // 定数バッファのサイズを256バイトアライメントに調整
    UINT bufferSize = (sizeof(DissolveParams) + 255) & ~255;
    
    // ヒーププロパティ
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    
    // リソースデスク
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = bufferSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    
    // リソースの作成
    HRESULT hr = directXCommon_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&constantBuffer_)
    );
    assert(SUCCEEDED(hr));
    
    // マッピング
    hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
    assert(SUCCEEDED(hr));
    
    // 初期値で更新
    UpdateConstantBuffer();
}

void Dissolve::LoadNoiseTexture()
{
    // ノイズテクスチャを読み込み
    auto& textureManager = TextureManager::GetInstance();
    auto texture = textureManager.Load("Assets/Texture/noise0.png");
    noiseTextureHandle_ = texture.gpuHandle;
}
}

