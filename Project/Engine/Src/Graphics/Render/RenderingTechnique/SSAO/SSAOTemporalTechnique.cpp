#include "pch.h"
#include "Utility/CVar/CVar.h"
#include "SSAOTemporalTechnique.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/RenderTarget/RenderTargetDescriptor.h"
#include "Graphics/Render/Pass/RenderPass.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImguiManager.h"
#endif

namespace CoreEngine
{
    namespace
    {
        CVar<bool> cvEnabled{
            "r.SSAOTemporal.Enabled", true,
            "SSAO のテンポラル蓄積を有効にする",
            CVarRange{}, CVarFlags::NoUI };
    }

    void SSAOTemporalTechnique::Initialize(DirectXCommon* dxCommon)
    {
        RenderingTechniqueBase::Initialize(dxCommon);
        cbRing_.Initialize(dxCommon, sizeof(SSAOTemporalParams));
    }

    const char* SSAOTemporalTechnique::GetHistoryTargetName(uint32_t index)
    {
        return (index == 0) ? RenderTargetNames::SSAOHistoryA : RenderTargetNames::SSAOHistoryB;
    }

    void SSAOTemporalTechnique::EnsureHistoryTargets(const RenderContext& context)
    {
        for (uint32_t index = 0; index < 2; ++index) {
            const char* targetName = GetHistoryTargetName(index);
            if (context.renderTargetManager->HasRenderTarget(targetName)) {
                continue;
            }

            RenderTargetDescriptor desc(targetName);
            desc.needsDepthStencil = false;
            // AO=1（遮蔽なし）でクリアしておくと初回参照でも画面が暗くならない
            desc.clearColor[0] = 1.0f;
            desc.clearColor[1] = 1.0f;
            desc.clearColor[2] = 1.0f;
            desc.clearColor[3] = 1.0f;
            context.renderTargetManager->CreateRenderTarget(desc);
        }
    }

    void SSAOTemporalTechnique::Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle)
    {
        outputSrvHandle = {};

        if (!IsEnabled() || !context.renderTargetManager || !context.frameBlackboard
            || !context.gBufferManager || !context.dxCommon) {
            return;
        }

        EnsureHistoryTargets(context);

        auto* cmdList = context.cmdList;

        const uint32_t writeIndex = GetWriteHistoryIndex(context.frameNumber);
        const uint32_t readIndex = 1u - writeIndex;

        RenderTarget* writeTarget = context.renderTargetManager->GetRenderTarget(GetHistoryTargetName(writeIndex));
        RenderTarget* readTarget = context.renderTargetManager->GetRenderTarget(GetHistoryTargetName(readIndex));
        RenderTarget* currentAO = context.renderTargetManager->GetRenderTarget(RenderTargetNames::SSAOBuffer);
        if (!writeTarget || !readTarget || !currentAO) {
            return;
        }

        // 前フレームから連続実行でなければ履歴は無効（TAATechnique と同じ判定）
        const bool continuous = historyValid_ && (context.frameNumber == lastExecutedFrame_ + 1ull);
        params_.disableHistory = continuous ? 0.0f : 1.0f;
        params_.screenSize[0] = static_cast<float>(context.gBufferManager->GetWidth());
        params_.screenSize[1] = static_cast<float>(context.gBufferManager->GetHeight());

        const D3D12_GPU_VIRTUAL_ADDRESS cbAddress =
            cbRing_.Upload(context.dxCommon, &params_, sizeof(params_));

        writeTarget->Begin(cmdList);

        cmdList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

        // t0: 今フレームの生 SSAO
        const int currentIdx = GetRootParamIndex("gCurrentAO");
        if (currentIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(currentIdx, currentAO->GetSRVHandle());
        }

        // t1: 前フレームの蓄積結果
        const int historyIdx = GetRootParamIndex("gHistoryAO");
        if (historyIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(historyIdx, readTarget->GetSRVHandle());
        }

        // t2: モーションベクター
        const int motionIdx = GetRootParamIndex("gMotionVector");
        if (motionIdx >= 0) {
            D3D12_GPU_DESCRIPTOR_HANDLE motionHandle{};
            if (context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::GBufferMotionVector, motionHandle)) {
                cmdList->SetGraphicsRootDescriptorTable(motionIdx, motionHandle);
            }
        }

        const int paramsIdx = GetRootParamIndex("SSAOTemporalParams");
        if (paramsIdx >= 0 && cbAddress != 0) {
            cmdList->SetGraphicsRootConstantBufferView(paramsIdx, cbAddress);
        }

        DrawFullscreenQuad(cmdList);

        writeTarget->End(cmdList);

        historyValid_ = true;
        lastExecutedFrame_ = context.frameNumber;

        outputSrvHandle = writeTarget->GetSRVHandle();
    }

    void SSAOTemporalTechnique::OnResize(uint32_t width, uint32_t height)
    {
        params_.screenSize[0] = static_cast<float>(width);
        params_.screenSize[1] = static_cast<float>(height);

        // 解像度が変わると履歴の画素対応が崩れるため破棄する
        historyValid_ = false;
    }

    void SSAOTemporalTechnique::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("SSAOTemporalParams");

        UI::SliderFloat("現フレーム寄与率", params_.blendAlpha, 0.02f, 0.5f);
        ImGui::TextWrapped("小さいほど AO が安定するが、動くものの影の追従が遅れる（0.1 前後が標準）");

        ImGui::Text("履歴: %s", historyValid_ ? "有効" : "無効（次フレームで再構築）");
        if (ImGui::Button("履歴をリセット")) {
            InvalidateHistory();
        }

        if (ImGui::Button("デフォルトに戻す")) {
            params_.blendAlpha = 0.1f;
        }

        ImGui::PopID();
#endif
    }

    const std::wstring& SSAOTemporalTechnique::GetPixelShaderPath() const
    {
        static const std::wstring path = L"SSAOTemporal.PS.hlsl";
        return path;
    }

    CVar<bool>* SSAOTemporalTechnique::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
