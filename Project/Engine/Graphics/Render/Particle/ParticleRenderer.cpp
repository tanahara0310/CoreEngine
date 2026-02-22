#include "ParticleRenderer.h"
#include "Engine/Particle/ParticleSystem.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Camera/ICamera.h"
#include <cassert>


namespace CoreEngine
{
void ParticleRenderer::Initialize(ID3D12Device* device) {
    // 基底クラスの初期化（共通処理）
    BaseParticleRenderer::Initialize(device);

    // ビルボード専用の頂点バッファを作成
    CreateSharedVertexBuffer();
}

void ParticleRenderer::OnBeginPass() {
    // 共有頂点バッファを設定
    cmdList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
}

void ParticleRenderer::Draw(ParticleSystem* particle) {
    // 基本的な検証
    if (!ValidateDrawCall(particle)) {
        return;
    }

    uint32_t instanceCount = particle->GetInstanceCount();

    // 共通リソースを設定
    SetupCommonResources(particle, particle->GetTextureHandle());

    // ビルボード描画コマンドを発行
    cmdList_->DrawInstanced(6, instanceCount, 0, 0);
}

void ParticleRenderer::CreatePSO() {
    // パーティクルのシェーダーコンパイル
    auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Particle/Particle.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);

    auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Particle/Particle.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    // ビルダーパターンでPSOを構築
    bool result = pipelineMg_->CreateBuilder()
        .SetInputLayoutFromReflection(*reflectionData_)
        .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
        .SetDepthStencil(true, false)
        .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
        .BuildAllBlendModes(device_, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

    if (!result) {
        throw std::runtime_error("Failed to create PSO in ParticleRenderer");
    }
}

void ParticleRenderer::CreateSharedVertexBuffer() {
    // パーティクル用の頂点データ（四角形）
    std::vector<VertexData> particleVertices = {
        { .position = { -1.0f,  1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 0.0f }, .normal = { 0.0f, 0.0f, -1.0f } }, // 左上
        { .position = {  1.0f,  1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 0.0f }, .normal = { 0.0f, 0.0f, -1.0f } }, // 右上
        { .position = {  1.0f, -1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 1.0f }, .normal = { 0.0f, 0.0f, -1.0f } }, // 右下

        { .position = { -1.0f,  1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 0.0f }, .normal = { 0.0f, 0.0f, -1.0f } }, // 左上
        { .position = {  1.0f, -1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 1.0f }, .normal = { 0.0f, 0.0f, -1.0f } }, // 右下
        { .position = { -1.0f, -1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 1.0f }, .normal = { 0.0f, 0.0f, -1.0f } }  // 左下
    };

    size_t vertexBufferSize = sizeof(VertexData) * particleVertices.size();

    // 頂点バッファのリソースを作成
    vertexBuffer_ = resourceFactory_->CreateBufferResource(device_, vertexBufferSize);

    // 頂点バッファにデータをコピー
    VertexData* vertexData = nullptr;
    vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, particleVertices.data(), vertexBufferSize);

    // 頂点バッファビューの設定
    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBufferSize);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
}
}
