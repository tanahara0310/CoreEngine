#include "pch.h"
#include "SSAOBlurTechnique.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Render/RenderManager.h"
#include "Math/MathCore.h"
#include <cstring>
#include <cassert>

namespace CoreEngine
{
    void SSAOBlurTechnique::Initialize(DirectXCommon* dxCommon)
    {
        RenderingTechniqueBase::Initialize(dxCommon);
        CreateConstantBuffer();
    }

    void SSAOBlurTechnique::Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle)
    {
        if (!IsEnabled() || !context.gBufferManager || !context.renderTargetManager) {
            outputSrvHandle = {};
            return;
        }

        auto* gBufferManager = context.gBufferManager;
        auto* renderTargetManager = context.renderTargetManager;
        auto* cmdList = context.dxCommon->GetCommandList();

        // スクリーンサイズの更新
        const float w = static_cast<float>(gBufferManager->GetWidth());
        const float h = static_cast<float>(gBufferManager->GetHeight());
        params_.screenWidth = w;
        params_.screenHeight = h;

        // 深度復元用 View*Projection 逆行列（ビューごとに更新）
        if (context.renderManager) {
            const Matrix4x4& view = context.renderManager->GetViewMatrix();
            const Matrix4x4& proj = context.renderManager->GetProjectionMatrix();
            const Matrix4x4 invViewProj = MathCore::Matrix::Inverse(MathCore::Matrix::Multiply(view, proj));
            std::memcpy(params_.invViewProjMatrix, &invViewProj, sizeof(float) * 16);
        }

        UpdateConstantBuffer();

        // SSAO と SSAOBlur のレンダーターゲットを取得
        auto* ssaoTarget = renderTargetManager->GetRenderTarget(RenderTargetNames::SSAOBuffer);
        auto* ssaoBlurTarget = renderTargetManager->GetRenderTarget(RenderTargetNames::SSAOBlurBuffer);

        if (!ssaoTarget || !ssaoBlurTarget) {
            outputSrvHandle = {};
            return;
        }

        // SSAOブラー描画
        ssaoBlurTarget->Begin(cmdList);

        cmdList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

        // t0: SSAO結果
        const int ssaoIdx = GetRootParamIndex("gTexture");
        if (ssaoIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(ssaoIdx, ssaoTarget->GetSRVHandle());
        }

        // t1: SceneDepth（WorldPosition ターゲット廃止に伴い、深度から復元する）
        const int depthIdx = GetRootParamIndex("gSceneDepth");
        if (depthIdx >= 0 && context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE depthHandle{};
            if (context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneDepth, depthHandle)) {
                cmdList->SetGraphicsRootDescriptorTable(depthIdx, depthHandle);
            }
        }

        // CBV: SSAOBlurParams
        const int paramsIdx = GetRootParamIndex("SSAOBlurParams");
        if (paramsIdx >= 0 && constantBuffer_) {
            cmdList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
        }

        DrawFullscreenQuad(cmdList);

        ssaoBlurTarget->End(cmdList);

        // 出力SRVハンドルを設定
        outputSrvHandle = ssaoBlurTarget->GetSRVHandle();
    }

    void SSAOBlurTechnique::OnResize(uint32_t width, uint32_t height)
    {
        params_.screenWidth = static_cast<float>(width);
        params_.screenHeight = static_cast<float>(height);
        UpdateConstantBuffer();
    }

    const std::wstring& SSAOBlurTechnique::GetPixelShaderPath() const
    {
        static const std::wstring path = L"SSAOBlur.PS.hlsl";
        return path;
    }

    void SSAOBlurTechnique::SetParams(const SSAOBlurParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void SSAOBlurTechnique::UpdateConstantBuffer()
    {
        if (mappedData_) {
            *mappedData_ = params_;
        }
    }

    void SSAOBlurTechnique::CreateConstantBuffer()
    {
        assert(directXCommon_);
        const UINT bufferSize = (sizeof(SSAOBlurParams) + 255) & ~255;
        constantBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), bufferSize);
        [[maybe_unused]] HRESULT hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();
    }
}
