#include "pch.h"
#include "Vignette.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Utility/CVar/CVar.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <cassert>


namespace CoreEngine
{
    namespace
    {
        // ヴィネットの調整パラメータ。ここで 1 行定義するだけで、
        // ImGui のスライダー・エディタ設定への自動保存・コンソールからの操作がすべて有効になる
        CVar<float> cvIntensity{
            "r.Vignette.Intensity", 0.8f,
            "ヴィネットの強さ。0 で無効、大きいほど四隅が暗くなる",
            CVarRange{ 0.0f, 2.0f } };

        CVar<float> cvSmoothness{
            "r.Vignette.Smoothness", 0.8f,
            "明暗の境界のなめらかさ。小さいほど境界がはっきりする",
            CVarRange{ 0.1f, 2.0f } };

        CVar<float> cvSize{
            "r.Vignette.Size", 16.0f,
            "効果のかかり始める半径。大きいほど画面中央の明るい領域が広がる",
            CVarRange{ 1.0f, 50.0f } };

        CVar<bool> cvEnabled{
            "r.Vignette.Enabled", false,
            "ヴィネットを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.Vignette";
    }

    void Vignette::OnCreateConstantBuffers()
    {
        UINT vignetteSize = (sizeof(VignetteParams) + 255) & ~255;
        vignetteParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), vignetteSize);
        HRESULT hr = vignetteParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedVignetteParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void Vignette::UpdateConstantBuffer()
    {
        if (!mappedVignetteParams_) {
            return;
        }
        // CVar が唯一のソース。差分検知を持たず毎フレーム書き込むが、
        // 16 バイトの書き込みなので比較コストを掛ける意味がない
        mappedVignetteParams_->intensity  = cvIntensity.Get();
        mappedVignetteParams_->smoothness = cvSmoothness.Get();
        mappedVignetteParams_->size       = cvSize.Get();
        mappedVignetteParams_->padding    = 0.0f;
    }

    void Vignette::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    Vignette::VignetteParams Vignette::GetParams() const
    {
        VignetteParams params;
        params.intensity  = cvIntensity.Get();
        params.smoothness = cvSmoothness.Get();
        params.size       = cvSize.Get();
        return params;
    }

    void Vignette::SetParams(const VignetteParams& params)
    {
        cvIntensity.Set(params.intensity);
        cvSmoothness.Set(params.smoothness);
        cvSize.Set(params.size);
        UpdateConstantBuffer();
    }

    void Vignette::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        // UI・コンソール・設定復元のいずれで値が変わっても、ここで毎フレーム取り込む
        UpdateConstantBuffer();
        UpdateScreenConstantBuffer(width, height);

        auto* cmdList = directXCommon_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx       = GetRootParamIndex("gTexture");
        int outputIdx        = GetRootParamIndex("gOutput");
        int vignetteParmsIdx = GetRootParamIndex("VignetteParams");
        int screenParamsIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0)       cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)        cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (vignetteParmsIdx >= 0) cmdList->SetComputeRootConstantBufferView(vignetteParmsIdx, vignetteParamsCB_->GetGPUVirtualAddress());
        if (screenParamsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenParamsIdx, screenParamsCB_->GetGPUVirtualAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void Vignette::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("VignetteParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        // パラメータ UI は CVar から自動生成される。
        // 項目を増やすときは Vignette.cpp 冒頭に CVar を 1 行足すだけでよい
        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* Vignette::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
