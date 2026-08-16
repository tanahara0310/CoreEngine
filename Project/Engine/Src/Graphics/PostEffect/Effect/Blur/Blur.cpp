#include "pch.h"
#include "Blur.h"
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
        CVar<float> cvIntensity{
            "r.Blur.Intensity", 1.0f,
            "ブラーの強度",
            CVarRange{ 0.0f, 5.0f } };

        CVar<float> cvKernelSize{
            "r.Blur.KernelSize", 1.0f,
            "サンプリング範囲の広さ。大きいほど広くぼける",
            CVarRange{ 0.5f, 3.0f } };

        CVar<bool> cvEnabled{
            "r.Blur.Enabled", false,
            "ガウシアンブラーを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.Blur";
    }

    void Blur::OnCreateConstantBuffers()
    {
        UINT blurSize = (sizeof(BlurParams) + 255) & ~255;
        blurParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), blurSize);
        [[maybe_unused]] HRESULT hr = blurParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedBlurParams_));
        assert(SUCCEEDED(hr));
        UpdateBlurConstantBuffer();

    }

    void Blur::UpdateBlurConstantBuffer()
    {
        if (!mappedBlurParams_) {
            return;
        }
        mappedBlurParams_->intensity  = cvIntensity.Get();
        mappedBlurParams_->kernelSize = cvKernelSize.Get();
    }

    void Blur::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        // CVar の現在値を取り込む（UI・コンソール・設定復元のいずれの変更もここで反映される）
        UpdateBlurConstantBuffer();
        UpdateScreenSizeConstants(width, height);

        auto* cmdList = directXCommon_->GetCommandList();

        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        // リソースをバインド（リフレクションでインデックスを取得）
        int textureIdx      = GetRootParamIndex("gTexture");
        int outputIdx       = GetRootParamIndex("gOutput");
        int blurParamsIdx   = GetRootParamIndex("BlurParams");
        int screenParamsIdx = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) {
            cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        }
        if (outputIdx >= 0) {
            cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        }
        if (blurParamsIdx >= 0) {
            cmdList->SetComputeRootConstantBufferView(blurParamsIdx, blurParamsCB_->GetGPUVirtualAddress());
        }
        if (screenParamsIdx >= 0) {
            cmdList->SetComputeRootConstantBufferView(screenParamsIdx, GetScreenSizeCbAddress());
        }

        // スレッドグループ数 = ceil(解像度 / 8)
        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void Blur::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("BlurParams");

        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();

        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }

        if (!IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
        }

        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* Blur::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
