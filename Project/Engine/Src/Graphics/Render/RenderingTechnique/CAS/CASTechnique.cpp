#include "pch.h"
#include "Utility/CVar/CVar.h"
#include "Editor/ImGui/CVarPanel.h"
#include "CASTechnique.h"
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
            "r.CAS.Enabled", true,
            "CAS（コントラスト適応シャープ）を有効にする",
            CVarRange{}, CVarFlags::NoUI };

        CVar<float> cvSharpness{
            "r.CAS.Sharpness", 0.5f,
            "シャープの強さ。0 = 最弱 / 1 = 最強",
            CVarRange{ 0.0f, 1.0f } };

        constexpr const char* kCVarPrefix = "r.CAS";
    }

    void CASTechnique::Initialize(DirectXCommon* dxCommon)
    {
        RenderingTechniqueBase::Initialize(dxCommon);
        CreateConstantBuffer();
    }

    void CASTechnique::Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle)
    {
        outputSrvHandle = {};

        // 調整値は CVar が保持する。UI・設定復元のどの経路で変わってもここで取り込む
        params_.sharpness = cvSharpness.Get();

        if (!IsEnabled() || !context.renderTargetManager || !context.frameBlackboard
            || !context.gBufferManager || !context.dxCommon) {
            return;
        }

        auto* cmdList = context.dxCommon->GetCommandList();

        RenderTarget* casTarget = context.renderTargetManager->GetRenderTarget(RenderTargetNames::CASOutput);
        if (!casTarget) {
            return;
        }

        // 入力が解決できない場合は何も描かない。
        // ここで空の CASOutput を後段へ渡すと画面が消えるため、必ず先に確認する。
        D3D12_GPU_DESCRIPTOR_HANDLE inputHandle{};
        if (!context.frameBlackboard->TryGetSrvHandle(inputResourceName_, inputHandle)) {
            return;
        }

        params_.screenSize[0] = static_cast<float>(context.gBufferManager->GetWidth());
        params_.screenSize[1] = static_cast<float>(context.gBufferManager->GetHeight());
        UpdateConstantBuffer();

        casTarget->Begin(cmdList);

        cmdList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

        const int sceneColorIdx = GetRootParamIndex("gSceneColor");
        if (sceneColorIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(sceneColorIdx, inputHandle);
        }

        const int paramsIdx = GetRootParamIndex("CASParams");
        if (paramsIdx >= 0 && constantBuffer_) {
            cmdList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
        }

        DrawFullscreenQuad(cmdList);

        casTarget->End(cmdList);

        outputSrvHandle = casTarget->GetSRVHandle();
    }

    void CASTechnique::OnResize(uint32_t width, uint32_t height)
    {
        params_.screenSize[0] = static_cast<float>(width);
        params_.screenSize[1] = static_cast<float>(height);
        UpdateConstantBuffer();
    }

    void CASTechnique::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("CASParams");

        // パラメータ UI は CVar から自動生成される（値は毎フレーム Execute で取り込まれる）
        CVarUI::DrawTree(kCVarPrefix);
        ImGui::TextWrapped("TAA でぼけた分を戻す。上げすぎると輪郭が硬くなる（0.5 前後が標準）");

        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }

        ImGui::PopID();
#endif
    }

    const std::wstring& CASTechnique::GetPixelShaderPath() const
    {
        static const std::wstring path = L"CAS.PS.hlsl";
        return path;
    }

    void CASTechnique::SetParams(const CASParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void CASTechnique::UpdateConstantBuffer()
    {
        if (mappedData_) {
            *mappedData_ = params_;
        }
    }

    void CASTechnique::CreateConstantBuffer()
    {
        assert(directXCommon_);
        const UINT bufferSize = (sizeof(CASParams) + 255) & ~255;
        constantBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), bufferSize);
        [[maybe_unused]] HRESULT hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();
    }

    CVar<bool>* CASTechnique::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
