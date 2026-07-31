#include "pch.h"
#include "SSAOTechnique.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Camera/View/ViewInfo.h"
#include "Scene/SceneManager.h"
#include "Math/MathCore.h"
#include <cstring>
#include <cassert>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImguiManager.h"
#endif

namespace CoreEngine
{
    void SSAOTechnique::Initialize(DirectXCommon* dxCommon)
    {
        RenderingTechniqueBase::Initialize(dxCommon);
        cbRing_.Initialize(dxCommon, sizeof(SSAOParams));
    }

    void SSAOTechnique::Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle)
    {
        if (!IsEnabled() || !context.gBufferManager || !context.renderManager || !context.renderTargetManager) {
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

        // カメラ行列の設定
        //
        // 実際に G-Buffer を描いたビューから取ること。単位行列や別カメラの行列で深度から
        // ワールド座標を復元すると座標が全く合わず、AO がノイズと黒斑（隣接面のちらつき）になる。
        // frameViews はフレーム先頭で確定した唯一のスナップショットで、TAA のジッタも
        // 織り込み済みなので G-Buffer と完全に整合する。
        if (!context.frameViews) {
            outputSrvHandle = {};
            return;
        }
        const ViewInfo& view = context.frameViews->GameView();
        if (!view.isValid) {
            outputSrvHandle = {};
            return;
        }

        std::memcpy(params_.viewMatrix, &view.viewMatrix, sizeof(float) * 16);
        std::memcpy(params_.projectionMatrix, &view.projection, sizeof(float) * 16);
        std::memcpy(params_.invViewProjMatrix, &view.invViewProjection, sizeof(float) * 16);

        // 今フレームのスライスへ書き込む（フレームオーバーラップ対応）
        const D3D12_GPU_VIRTUAL_ADDRESS cbAddress =
            cbRing_.Upload(context.dxCommon, &params_, sizeof(params_));

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

        // t1: SceneDepth（WorldPosition ターゲット廃止に伴い、深度から復元する）
        // ビュー別（ゲーム/反射）に差し替わる FrameBlackboard 経由で取得する
        // （dxCommon の深度は常にゲームビュー本解像度のため反射ビューでは不整合になる）。
        const int depthIdx = GetRootParamIndex("gSceneDepth");
        if (depthIdx >= 0 && context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE depthHandle{};
            if (context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneDepth, depthHandle)) {
                cmdList->SetGraphicsRootDescriptorTable(depthIdx, depthHandle);
            }
        }

        // CBV: SSAOParams
        const int paramsIdx = GetRootParamIndex("SSAOParams");
        if (paramsIdx >= 0 && cbAddress != 0) {
            cmdList->SetGraphicsRootConstantBufferView(paramsIdx, cbAddress);
        }

        DrawFullscreenQuad(cmdList);

        ssaoTarget->End(cmdList);

        // 出力SRVハンドルを設定
        outputSrvHandle = ssaoTarget->GetSRVHandle();
    }

    void SSAOTechnique::OnResize(uint32_t width, uint32_t height)
    {
        // 定数は毎フレーム Execute でリングへ書かれるため、CPU 側の値だけ更新すればよい
        params_.screenWidth = static_cast<float>(width);
        params_.screenHeight = static_cast<float>(height);
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

        (void)changed; // 定数は毎フレーム Execute でリングへ書かれる

        if (ImGui::Button("デフォルトに戻す")) {
            params_.radius      = 0.5f;
            params_.bias        = 0.025f;
            params_.intensity   = 1.0f;
            params_.power       = 1.5f;
            params_.sampleCount = 16;
        }

        ImGui::PopID();
#endif
    }

    const std::wstring& SSAOTechnique::GetPixelShaderPath() const
    {
        static const std::wstring path = L"SSAO.PS.hlsl";
        return path;
    }
}
