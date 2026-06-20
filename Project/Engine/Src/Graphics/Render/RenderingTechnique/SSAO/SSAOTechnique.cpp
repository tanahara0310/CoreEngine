#include "pch.h"
#include "SSAOTechnique.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include <cstring>
#include <cassert>

#ifdef USE_IMGUI
#include "Utility/Debug/ImGui/ImguiManager.h"
#endif

namespace CoreEngine
{
    void SSAOTechnique::Initialize(DirectXCommon* dxCommon)
    {
        RenderingTechniqueBase::Initialize(dxCommon);
        CreateConstantBuffer();
    }

    void SSAOTechnique::Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle)
    {
        if (!IsEnabled() || !context.gBufferManager || !context.renderManager || !context.renderTargetManager) {
            outputSrvHandle = {};
            return;
        }

        auto* gBufferManager = context.gBufferManager;
        auto* renderManager = context.renderManager;
        auto* renderTargetManager = context.renderTargetManager;
        auto* cmdList = context.dxCommon->GetCommandList();

        // スクリーンサイズの更新
        const float w = static_cast<float>(gBufferManager->GetWidth());
        const float h = static_cast<float>(gBufferManager->GetHeight());
        params_.screenWidth = w;
        params_.screenHeight = h;

        // カメラ行列の設定
        const Matrix4x4& view = renderManager->GetViewMatrix();
        const Matrix4x4& proj = renderManager->GetProjectionMatrix();
        std::memcpy(params_.viewMatrix, &view, sizeof(float) * 16);
        std::memcpy(params_.projectionMatrix, &proj, sizeof(float) * 16);

        UpdateConstantBuffer();

        // SSAO用のレンダーターゲットを取得
        auto* ssaoTarget = renderTargetManager->GetRenderTarget(RenderTargetNames::SSAOBuffer);
        if (!ssaoTarget) {
            outputSrvHandle = {};
            return;
        }

        // SSAO描画
        ssaoTarget->Begin(cmdList);

        cmdList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

        // t0: NormalRoughness
        const int normalIdx = GetRootParamIndex("gNormalRoughness");
        if (normalIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                normalIdx,
                gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness));
        }

        // t1: WorldPosition
        const int posIdx = GetRootParamIndex("gWorldPosition");
        if (posIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(
                posIdx,
                gBufferManager->GetSRVHandle(GBufferManager::Target::WorldPosition));
        }

        // CBV: SSAOParams
        const int paramsIdx = GetRootParamIndex("SSAOParams");
        if (paramsIdx >= 0 && constantBuffer_) {
            cmdList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
        }

        DrawFullscreenQuad(cmdList);

        ssaoTarget->End(cmdList);

        // 出力SRVハンドルを設定
        outputSrvHandle = ssaoTarget->GetSRVHandle();
    }

    void SSAOTechnique::OnResize(uint32_t width, uint32_t height)
    {
        params_.screenWidth = static_cast<float>(width);
        params_.screenHeight = static_cast<float>(height);
        UpdateConstantBuffer();
    }

    void SSAOTechnique::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("SSAOParams");

        bool changed = false;
        if (ImGui::TreeNode("パラメータ")) {
            if (UI::SliderFloat("半径", params_.radius, 0.05f, 2.0f))    { changed = true; }
            if (UI::SliderFloat("バイアス", params_.bias, 0.001f, 0.1f)) { changed = true; }
            if (UI::SliderFloat("強度", params_.intensity, 0.0f, 3.0f))  { changed = true; }
            if (UI::SliderFloat("べき乗", params_.power, 0.5f, 4.0f))    { changed = true; }
            if (ImGui::SliderInt("サンプル数", &params_.sampleCount, 4, 64)) { changed = true; }
            ImGui::TreePop();
        }

        if (changed) {
            UpdateConstantBuffer();
        }

        if (ImGui::Button("デフォルトに戻す")) {
            params_.radius      = 0.5f;
            params_.bias        = 0.025f;
            params_.intensity   = 1.0f;
            params_.power       = 1.5f;
            params_.sampleCount = 16;
            UpdateConstantBuffer();
        }

        ImGui::PopID();
#endif
    }

    const std::wstring& SSAOTechnique::GetPixelShaderPath() const
    {
        static const std::wstring path = L"SSAO.PS.hlsl";
        return path;
    }

    void SSAOTechnique::SetParams(const SSAOParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void SSAOTechnique::UpdateConstantBuffer()
    {
        if (mappedData_) {
            *mappedData_ = params_;
        }
    }

    void SSAOTechnique::CreateConstantBuffer()
    {
        assert(directXCommon_);
        const UINT bufferSize = (sizeof(SSAOParams) + 255) & ~255;
        constantBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), bufferSize);
        [[maybe_unused]] HRESULT hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();
    }
}
