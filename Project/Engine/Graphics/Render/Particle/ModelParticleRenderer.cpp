#include "ModelParticleRenderer.h"
#include "Engine/Particle/ParticleSystem.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Graphics/Model/ModelResource.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Graphics/TextureManager.h"
#include <cassert>


namespace CoreEngine
{
void ModelParticleRenderer::Draw(ParticleSystem* particle) {
    // 基本的な検証
    if (!ValidateDrawCall(particle)) {
        return;
    }

    // モデルパーティクルかチェック
    if (!particle->IsModelParticle()) {
        return;
    }

    ModelResource* modelResource = particle->GetModelResource();
    if (!modelResource || !modelResource->IsLoaded()) {
        return;
    }

    uint32_t instanceCount = particle->GetInstanceCount();

    // モデルの頂点バッファとインデックスバッファを設定
    cmdList_->IASetVertexBuffers(0, 1, &modelResource->vertexBufferView_);
    cmdList_->IASetIndexBuffer(&modelResource->indexBufferView_);

    // テクスチャハンドルを決定（パーティクル設定 > モデルデフォルト）
    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = particle->GetTextureHandle();
    if (textureHandle.ptr == 0) {
        // モデルの最初のマテリアルのテクスチャを使用
        const auto& materials = modelResource->GetMaterials();
        if (!materials.empty() && !materials[0].baseColorTexture.empty()) {
            textureHandle = TextureManager::GetInstance().Load(materials[0].baseColorTexture).gpuHandle;
        }
    }

    // 共通リソースを設定
    SetupCommonResources(particle, textureHandle);

    // インスタンシング描画（モデルのインデックス数 × インスタンス数）
    cmdList_->DrawIndexedInstanced(modelResource->indexCount_, instanceCount, 0, 0, 0);
}

void ModelParticleRenderer::CreatePSO() {
    // モデルパーティクル用のシェーダーコンパイル
    auto vertexShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Particle/ModelParticle.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);

    auto pixelShaderBlob = shaderCompiler_->CompileShader(L"Assets/Shaders/Particle/Particle.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    // モデルパーティクル用リフレクション（基底クラスとは別シェーダー）
    auto modelParticleReflection = reflectionBuilder_->BuildFromShaders(
        vertexShaderBlob, pixelShaderBlob, "ModelParticleRenderer");

    // ビルダーパターンでPSOを構築（入力レイアウト自動化）
    bool result = pipelineMg_->CreateBuilder()
        .SetInputLayoutFromReflection(*modelParticleReflection)
        .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
        .SetDepthStencil(true, true)  // 深度テストと深度書き込みを有効化
        .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
        .BuildAllBlendModes(device_, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

    if (!result) {
        throw std::runtime_error("Failed to create PSO in ModelParticleRenderer");
    }
}
}
