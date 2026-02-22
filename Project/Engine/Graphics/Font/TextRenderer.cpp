#include "TextRenderer.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Graphics/Structs/SpriteMaterial.h"
#include "Engine/Graphics/Shader/ShaderReflectionData.h"
#include "Engine/Graphics/RootSignature/RootSignatureConfig.h"
#include "WinApp/WinApp.h"
#include <cassert>


namespace CoreEngine
{
void TextRenderer::Initialize(ID3D12Device* device) {
    shaderCompiler_->Initialize();
    reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());

    auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Text/Text.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);

    auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Text/Text.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    // リフレクション
    reflectionData_ = reflectionBuilder_->BuildFromShaders(vertexShaderBlob, pixelShaderBlob, "TextRenderer");

    // シンプルな設定でRootSignatureを構築
    RootSignatureConfig config = RootSignatureConfig::Simple();
    config.ConfigureSampler("gSampler", SamplerConfig::Linear());

    auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);

    if (!buildResult.success) {
        throw std::runtime_error("Failed to create Text Root Signature: " + buildResult.errorMessage);
    }

    bool result = psoMg_->CreateBuilder()
        .SetInputLayoutFromReflection(*reflectionData_)
        .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
        .SetDepthStencil(false, false)
        .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
        .BuildAllBlendModes(device, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

    if (!result) {
        throw std::runtime_error("Failed to create Text Pipeline State Object");
    }

    pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNormal);
    currentBlendMode_ = BlendMode::kBlendModeNormal;
}

int TextRenderer::GetRootParamIndex(const std::string& resourceName) const {
    if (!reflectionData_) {
        return -1;
    }
    return reflectionData_->GetRootParameterIndexByName(resourceName);
}

void TextRenderer::Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory) {
    dxCommon_ = dxCommon;
    resourceFactory_ = resourceFactory;

    Initialize(dxCommon->GetDevice());

    for (UINT frameIndex = 0; frameIndex < kFrameCount; ++frameIndex) {
        auto& matResources = materialResources_[frameIndex];
        auto& tfResources = transformResources_[frameIndex];
        auto& matData = materialDataPool_[frameIndex];
        auto& tfData = transformDataPool_[frameIndex];

        matResources.resize(kMaxGlyphCount);
        tfResources.resize(kMaxGlyphCount);
        matData.resize(kMaxGlyphCount);
        tfData.resize(kMaxGlyphCount);

        for (size_t i = 0; i < kMaxGlyphCount; ++i) {
            // 永続マッピング（D3D12_HEAP_TYPE_UPLOADでは推奨される方法）
            // Microsoft公式: UPLOAD_BUFFERは永続的にマップしたままにするべき
            matResources[i] = resourceFactory_->CreateBufferResource(dxCommon_->GetDevice(), sizeof(SpriteMaterial));
            matResources[i]->Map(0, nullptr, reinterpret_cast<void**>(&matData[i]));

            tfResources[i] = resourceFactory_->CreateBufferResource(dxCommon_->GetDevice(), sizeof(TransformationMatrix));
            tfResources[i]->Map(0, nullptr, reinterpret_cast<void**>(&tfData[i]));
        }
    }
}

void TextRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
    currentBufferIndex_ = 0;
    currentFrameIndex_ = dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();

    if (blendMode != currentBlendMode_) {
        currentBlendMode_ = blendMode;
        pipelineState_ = psoMg_->GetPipelineState(blendMode);
    }

    cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
    cmdList->SetPipelineState(pipelineState_);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void TextRenderer::EndPass() {
}

void TextRenderer::SetCamera(const ICamera* camera) {
    (void)camera;
}

size_t TextRenderer::GetAvailableConstantBuffer() {
    // グリフ数の上限チェック
    if (currentBufferIndex_ >= kMaxGlyphCount) {
        #ifdef _DEBUG
            OutputDebugStringA("ERROR: Glyph count exceeded maximum! Increase kMaxGlyphCount or reduce text length.\n");
        #endif
        // 上限に達した場合は最後のバッファを再利用（クラッシュ防止）
        return kMaxGlyphCount - 1;
    }
    
    size_t bufferIndex = currentBufferIndex_;
    currentBufferIndex_++;
    return bufferIndex;
}

Matrix4x4 TextRenderer::CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation) const {
    Matrix4x4 worldMatrix = MathCore::Matrix::MakeAffine(scale, rotation, position);
    Matrix4x4 viewMatrix = MathCore::Matrix::Identity();
    Matrix4x4 projectionMatrix = MathCore::Rendering::Orthographic(
        0.0f, 0.0f,
        static_cast<float>(WinApp::kClientWidth),
        static_cast<float>(WinApp::kClientHeight),
        0.0f, 100.0f);

    return MathCore::Matrix::Multiply(worldMatrix, MathCore::Matrix::Multiply(viewMatrix, projectionMatrix));
}

Matrix4x4 TextRenderer::CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation, const ICamera* camera) const {
    Matrix4x4 worldMatrix = MathCore::Matrix::MakeAffine(scale, rotation, position);

    if (camera) {
        Matrix4x4 viewMatrix = camera->GetViewMatrix();
        Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();
        return MathCore::Matrix::Multiply(worldMatrix, MathCore::Matrix::Multiply(viewMatrix, projectionMatrix));
    }
    else {
        Matrix4x4 viewMatrix = MathCore::Matrix::Identity();
        Matrix4x4 projectionMatrix = MathCore::Rendering::Orthographic(
            0.0f, 0.0f,
            static_cast<float>(WinApp::kClientWidth),
            static_cast<float>(WinApp::kClientHeight),
            0.0f, 100.0f);
        return MathCore::Matrix::Multiply(worldMatrix, MathCore::Matrix::Multiply(viewMatrix, projectionMatrix));
    }
}
}
