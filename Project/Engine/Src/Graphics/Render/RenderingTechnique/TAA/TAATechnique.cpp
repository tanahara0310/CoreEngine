#include "pch.h"
#include "Utility/CVar/CVar.h"
#include "Editor/ImGui/CVarPanel.h"
#include "TAATechnique.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include <cassert>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImguiManager.h"
#endif

namespace CoreEngine
{
    namespace
    {
        CVar<bool> cvEnabled{
            "r.TAA.Enabled", true,
            "TAA（テンポラル アンチエイリアシング）を有効にする",
            CVarRange{}, CVarFlags::NoUI };

        CVar<float> cvBlendAlpha{
            "r.TAA.BlendAlpha", 0.1f,
            "現フレームの寄与率。小さいほど滑らかだが残像寄りになる",
            CVarRange{ 0.01f, 1.0f } };

        CVar<float> cvBlendAlphaMax{
            "r.TAA.BlendAlphaMax", 0.9f,
            "履歴と現フレームが食い違う画素で使う寄与率の上限"
            "（水面など毎フレーム表面が変わる面のぼけ対策。blendAlpha と同値にすると従来動作）",
            CVarRange{ 0.01f, 1.0f } };

        CVar<float> cvClampScale{
            "r.TAA.ClampScale", 1.0f,
            "近傍 AABB の拡張率。大きいほどゴーストが出やすく、小さいほどちらつく",
            CVarRange{ 0.1f, 4.0f } };

        constexpr const char* kCVarPrefix = "r.TAA";
    }

    void TAATechnique::Initialize(DirectXCommon* dxCommon)
    {
        RenderingTechniqueBase::Initialize(dxCommon);
        cbRing_.Initialize(dxCommon, sizeof(TAAParams));
    }

    const char* TAATechnique::GetHistoryTargetName(uint32_t index)
    {
        return (index == 0) ? RenderTargetNames::TAAHistoryA : RenderTargetNames::TAAHistoryB;
    }

    void TAATechnique::Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle)
    {
        outputSrvHandle = {};

        // 調整値は CVar が保持する。UI・設定復元のどの経路で変わってもここで取り込む
        params_.blendAlpha = cvBlendAlpha.Get();
        params_.blendAlphaMax = (std::max)(cvBlendAlphaMax.Get(), params_.blendAlpha);
        params_.clampScale = cvClampScale.Get();

        if (!IsEnabled() || !context.renderTargetManager || !context.frameBlackboard
            || !context.gBufferManager || !context.dxCommon) {
            return;
        }

        auto* renderTargetManager = context.renderTargetManager;
        auto* cmdList = context.dxCommon->GetCommandList();

        // 書き込み先は frameNumber の偶奇だけで決まる（RenderPipeline の論理名登録と同じ基準）
        const uint32_t writeIndex = GetWriteHistoryIndex(context.frameNumber);
        const uint32_t readIndex = 1u - writeIndex;

        RenderTarget* writeTarget = renderTargetManager->GetRenderTarget(GetHistoryTargetName(writeIndex));
        RenderTarget* readTarget = renderTargetManager->GetRenderTarget(GetHistoryTargetName(readIndex));
        if (!writeTarget || !readTarget) {
            return;
        }

        // 前フレームから連続して実行されていなければ履歴は信用できない。
        // （TAA を途中で有効化した / シーンを切り替えた / リサイズした 直後）
        const bool continuous = historyValid_ && (context.frameNumber == lastExecutedFrame_ + 1ull);
        params_.disableHistory = continuous ? 0.0f : 1.0f;

        params_.screenSize[0] = static_cast<float>(context.gBufferManager->GetWidth());
        params_.screenSize[1] = static_cast<float>(context.gBufferManager->GetHeight());

        // 今フレームのスライスへ書き込む（フレームオーバーラップ対応）
        const D3D12_GPU_VIRTUAL_ADDRESS cbAddress =
            cbRing_.Upload(context.dxCommon, &params_, sizeof(params_));

        writeTarget->Begin(cmdList);

        cmdList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

        // t0: 現フレームの SceneColor（ビュー依存のため Blackboard 経由で取る）
        const int sceneColorIdx = GetRootParamIndex("gSceneColor");
        if (sceneColorIdx >= 0) {
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorHandle{};
            if (context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneColor, sceneColorHandle)) {
                cmdList->SetGraphicsRootDescriptorTable(sceneColorIdx, sceneColorHandle);
            }
        }

        // t1: 前フレームの TAA 出力
        const int historyIdx = GetRootParamIndex("gHistoryColor");
        if (historyIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(historyIdx, readTarget->GetSRVHandle());
        }

        // t2: モーションベクター（GBuffer 産の NDC 差分）
        const int motionIdx = GetRootParamIndex("gMotionVector");
        if (motionIdx >= 0) {
            D3D12_GPU_DESCRIPTOR_HANDLE motionHandle{};
            if (context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::GBufferMotionVector, motionHandle)) {
                cmdList->SetGraphicsRootDescriptorTable(motionIdx, motionHandle);
            }
        }

        const int paramsIdx = GetRootParamIndex("TAAParams");
        if (paramsIdx >= 0 && cbAddress != 0) {
            cmdList->SetGraphicsRootConstantBufferView(paramsIdx, cbAddress);
        }

        DrawFullscreenQuad(cmdList);

        writeTarget->End(cmdList);

        historyValid_ = true;
        lastExecutedFrame_ = context.frameNumber;

        outputSrvHandle = writeTarget->GetSRVHandle();
    }

    void TAATechnique::OnResize(uint32_t width, uint32_t height)
    {
        params_.screenSize[0] = static_cast<float>(width);
        params_.screenSize[1] = static_cast<float>(height);

        // 解像度が変わると履歴の画素対応が崩れるため破棄する
        historyValid_ = false;
    }

    void TAATechnique::SetJitter(float ndcX, float ndcY, uint64_t frameNumber)
    {
        // 同一フレーム内の再呼び出し（View が複数ある場合）では前回値を進めない。
        // 進めてしまうと差分が 0 になり、ジッタ補正が効かなくなる。
        if (jitterInitialized_ && frameNumber == lastJitterFrame_) {
            return;
        }

        // フレームが飛んでいる場合は前フレームのジッタが当てにならないので補正しない
        const bool continuous = jitterInitialized_ && (frameNumber == lastJitterFrame_ + 1ull);
        params_.jitterDelta[0] = continuous ? (ndcX - prevJitterX_) : 0.0f;
        params_.jitterDelta[1] = continuous ? (ndcY - prevJitterY_) : 0.0f;

        prevJitterX_ = ndcX;
        prevJitterY_ = ndcY;
        lastJitterFrame_ = frameNumber;
        jitterInitialized_ = true;
    }

    void TAATechnique::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("TAAParams");

        // パラメータ UI は CVar から自動生成される（値は毎フレーム Execute で取り込まれる）
        CVarUI::DrawTree(kCVarPrefix);

        ImGui::Text("履歴: %s", historyValid_ ? "有効" : "無効（次フレームで再構築）");
        if (ImGui::Button("履歴をリセット")) {
            InvalidateHistory();
        }

        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }

        ImGui::PopID();
#endif
    }

    const std::wstring& TAATechnique::GetPixelShaderPath() const
    {
        static const std::wstring path = L"TAA.PS.hlsl";
        return path;
    }

    CVar<bool>* TAATechnique::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
