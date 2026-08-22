#include "pch.h"
#include "Render.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/RenderTarget/RenderTargetDescriptor.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"

using namespace Microsoft::WRL;


namespace CoreEngine
{
    void Render::Initialize(GraphicsCore* dxCommon, ComPtr<ID3D12DescriptorHeap> dsvHeap)
    {
        dxCommon_ = dxCommon;
        dsvHeap_ = dsvHeap;

        // RenderTargetManagerの初期化
        renderTargetManager_ = std::make_unique<RenderTargetManager>();
        renderTargetManager_->Initialize(dxCommon, dsvHeap);

        // 現在の SceneColor 既定ターゲットを作成
        RenderTargetDescriptor sceneColorDesc(RenderTargetNames::SceneColor);
        sceneColorDesc.clearColor[0] = kClearColor[0];
        sceneColorDesc.clearColor[1] = kClearColor[1];
        sceneColorDesc.clearColor[2] = kClearColor[2];
        sceneColorDesc.clearColor[3] = kClearColor[3];
        renderTargetManager_->CreateRenderTarget(sceneColorDesc);

        RenderTargetDescriptor sceneColorSnapshotDesc(RenderTargetNames::SceneColorSnapshot);
        sceneColorSnapshotDesc.clearColor[0] = kClearColor[0];
        sceneColorSnapshotDesc.clearColor[1] = kClearColor[1];
        sceneColorSnapshotDesc.clearColor[2] = kClearColor[2];
        sceneColorSnapshotDesc.clearColor[3] = kClearColor[3];
        sceneColorSnapshotDesc.needsDepthStencil = false;
        if (auto* sceneColorSnapshot = renderTargetManager_->CreateRenderTarget(sceneColorSnapshotDesc)) {
            if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(sceneColorSnapshot)) {
                offscreen->SetUseDepthBuffer(false);
            }
        }

        renderTargetManager_->EnsurePostEffectFinalTarget();

        // SSAO用バッファ
        // フルスクリーンポストプロセスのためDSVは不要（深度バッファを破壊しないようにする）
        // クリア色は白（AO無し = 1.0）。リソース作成時と ClearRenderTargetView 時を一致させる
        static constexpr float kSSAOClearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        RenderTargetDescriptor ssaoDesc(RenderTargetNames::SSAOBuffer);
        ssaoDesc.clearColor[0] = kSSAOClearColor[0];
        ssaoDesc.clearColor[1] = kSSAOClearColor[1];
        ssaoDesc.clearColor[2] = kSSAOClearColor[2];
        ssaoDesc.clearColor[3] = kSSAOClearColor[3];
        ssaoDesc.needsDepthStencil = false;
        if (auto* ssaoTarget = renderTargetManager_->CreateRenderTarget(ssaoDesc)) {
            if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(ssaoTarget)) {
                offscreen->SetUseDepthBuffer(false);
            }
        }

        RenderTargetDescriptor ssaoBlurDesc(RenderTargetNames::SSAOBlurBuffer);
        ssaoBlurDesc.clearColor[0] = kSSAOClearColor[0];
        ssaoBlurDesc.clearColor[1] = kSSAOClearColor[1];
        ssaoBlurDesc.clearColor[2] = kSSAOClearColor[2];
        ssaoBlurDesc.clearColor[3] = kSSAOClearColor[3];
        ssaoBlurDesc.needsDepthStencil = false;
        if (auto* ssaoBlurTarget = renderTargetManager_->CreateRenderTarget(ssaoBlurDesc)) {
            if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(ssaoBlurTarget)) {
                offscreen->SetUseDepthBuffer(false);
            }
        }

        // バックバッファターゲットを作成
        renderTargetManager_->CreateBackBufferTarget(RenderTargetNames::BackBuffer);
    }

    RenderTarget* Render::GetRenderTarget(const std::string& name)
    {
        return renderTargetManager_->GetRenderTarget(name);
    }

    void Render::OnWindowResize(int32_t width, int32_t height)
    {
        renderTargetManager_->ResizeAutoTargets(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    }

    void Render::FinalizeFrame()
    {
        // バックバッファを PRESENT 状態へ戻すところまでが Render の責務。
        // Close / Execute / Signal / Present / 次フレームの準備は
        // GraphicsCore::EndFrame() が一括で行う（フレーム手順の単一ソース）。
        auto* backBuffer = renderTargetManager_->GetRenderTarget(RenderTargetNames::BackBuffer);
        if (backBuffer) {
            backBuffer->End(dxCommon_->GetCommandList());
        }
    }
}
