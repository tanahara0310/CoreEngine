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

#include "Utility/CVar/CVar.h"
#include "Editor/ImGui/CVarPanel.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/ImguiManager.h"
#endif

namespace CoreEngine
{
    namespace
    {
        CVar<bool> cvEnabled{
            "r.SSAO.Enabled", true,
            "SSAO（スクリーンスペース アンビエントオクルージョン）を有効にする",
            CVarRange{}, CVarFlags::NoUI };

        CVar<float> cvRadius{
            "r.SSAO.Radius", 0.5f,
            "遮蔽を探すサンプル半径（ワールド単位）",
            CVarRange{ 0.05f, 2.0f } };

        CVar<float> cvBias{
            "r.SSAO.Bias", 0.025f,
            "自己遮蔽を防ぐ深度バイアス",
            CVarRange{ 0.001f, 0.1f } };

        CVar<float> cvIntensity{
            "r.SSAO.Intensity", 1.0f,
            "遮蔽の強さ",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> cvPower{
            "r.SSAO.Power", 1.5f,
            "遮蔽値のべき乗。大きいほどコントラストが強くなる",
            CVarRange{ 0.5f, 4.0f } };

        CVar<int> cvSampleCount{
            "r.SSAO.SampleCount", 16,
            "1 ピクセルあたりのサンプル数。多いほど滑らかだが重い",
            CVarRange{ 4.0f, 64.0f } };

        constexpr const char* kCVarPrefix = "r.SSAO";
    }

    void SSAOTechnique::Initialize(DirectXCommon* dxCommon)
    {
        RenderingTechniqueBase::Initialize(dxCommon);
        cbRing_.Initialize(dxCommon, sizeof(SSAOParams));
    }

    void SSAOTechnique::Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle)
    {
        // 調整値は CVar が保持する。UI・設定復元のどの経路で変わってもここで取り込む
        params_.radius      = cvRadius.Get();
        params_.bias        = cvBias.Get();
        params_.intensity   = cvIntensity.Get();
        params_.power       = cvPower.Get();
        params_.sampleCount = cvSampleCount.Get();

        if (!IsEnabled() || !context.gBufferManager || !context.renderManager || !context.renderTargetManager) {
            outputSrvHandle = {};
            return;
        }

        auto* gBufferManager = context.gBufferManager;
        auto* renderTargetManager = context.renderTargetManager;
        auto* cmdList = context.cmdList;

        // スクリーンサイズの更新
        const float w = static_cast<float>(gBufferManager->GetWidth());
        const float h = static_cast<float>(gBufferManager->GetHeight());
        params_.screenWidth = w;
        params_.screenHeight = h;

        // カメラ行列は実際に G-Buffer を描いたビューから取ること。
        // 別カメラの行列で深度からワールド座標を復元すると、AO がノイズと黒斑になる。
        // frameViews はフレーム先頭で確定した唯一のスナップショットで、TAA のジッタも織り込み済み。
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

        // パラメータ UI は CVar から自動生成される（値は毎フレーム Execute で取り込まれる）
        CVarUI::DrawTree(kCVarPrefix);

        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }

        ImGui::PopID();
#endif
    }

    const std::wstring& SSAOTechnique::GetPixelShaderPath() const
    {
        static const std::wstring path = L"SSAO.PS.hlsl";
        return path;
    }

    CVar<bool>* SSAOTechnique::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
