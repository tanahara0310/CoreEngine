#include "pch.h"
#include "ModelParticleRenderer.h"
#include "Particle/ParticleSystem.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Model/ModelResource.h"
#include "Camera/Camera.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include <cassert>


namespace CoreEngine
{
    void ModelParticleRenderer::Initialize(ID3D12Device* device) {
        BaseParticleRenderer::Initialize(device);

        // ルートパラメータは描画のたびに名前で引かず、ここで一度だけ解決する
        lightCountsSlot_ = reflectionData_->GetRootSlot("gLightCounts");
        directionalLightsSlot_ = reflectionData_->GetRootSlot("gDirectionalLights");
    }

    void ModelParticleRenderer::OnBeginPass() {
        if (!lightManager_) {
            return;
        }

        // ポイント／スポット／エリアライトは ModelParticle.PS.hlsl が参照しないので
        // 未解決スロット（＝ShaderBinder 側で no-op）を渡す。
        ShaderBinder binder(cmdList_, ShaderBinder::Pipeline::Graphics);
        lightManager_->SetLightsToCommandList(
            binder, lightCountsSlot_, directionalLightsSlot_,
            RootSlot{}, RootSlot{}, RootSlot{});
    }

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
        cmdList_->IASetVertexBuffers(0, 1, &modelResource->GetVertexBufferView());
        cmdList_->IASetIndexBuffer(&modelResource->GetIndexBufferView());

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
        cmdList_->DrawIndexedInstanced(modelResource->GetIndexCount(), instanceCount, 0, 0, 0);
    }

    void ModelParticleRenderer::CreatePSO() {
        // モデルパーティクル用のシェーダーコンパイル
        auto vertexShaderBlob = shaderCompiler_->CompileShader(GetVertexShaderPath(), L"vs_6_0");
        assert(vertexShaderBlob != nullptr);

        auto pixelShaderBlob = shaderCompiler_->CompileShader(GetPixelShaderPath(), L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        // 入力レイアウトは CreateRootSignature が同じシェーダーから取ったリフレクションを流用する。
        // ここで別に取り直すと、RS と PSO でシェーダーがずれても気付けない。

        // ビルダーパターンでPSOを構築（入力レイアウト自動化）
        bool result = psoMg_->CreateBuilder()
            .SetDebugName("ModelParticle")
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_BACK, D3D12_FILL_MODE_SOLID)
            // 深度テスト・書き込みとも有効。
            // 書き込みを切ると、後段の SkyBox / VolumetricCloud と、同パス内で後に来る
            // グリッド・ラインに塗り潰されて粒が消える（板ポリと違い不透明として置ける
            // モデルパーティクル固有の要件）。パス側の宣言は ForwardQueuePassBase が
            // SceneDepth を DEPTH_WRITE で持つように直してある。
            .SetDepthStencil(true, true)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device_, vertexShaderBlob, pixelShaderBlob, rootSignatureMg_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create PSO in ModelParticleRenderer");
        }
    }
}
