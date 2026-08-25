#include "pch.h"
#include "Dissolve.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/CVar/CVar.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <cassert>


namespace CoreEngine
{
    namespace
    {
        CVar<float> cvThreshold{
            "r.Dissolve.Threshold", 0.0f,
            "消滅の進行度。1 で完全に消える",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvEdgeWidth{
            "r.Dissolve.EdgeWidth", 0.1f,
            "消滅境界に出る発光エッジの幅",
            CVarRange{ 0.0f, 0.5f } };

        CVar<Vector3> cvEdgeColor{
            "r.Dissolve.EdgeColor", Vector3{ 1.0f, 0.5f, 0.0f },
            "エッジの発光色（RGB）",
            CVarRange{ 0.0f, 1.0f } };

        CVar<bool> cvEnabled{
            "r.Dissolve.Enabled", false,
            "ディゾルブを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.Dissolve";
    }

    void Dissolve::OnCreateConstantBuffers()
    {
        // 確保サイズは C++ の sizeof ではなく「HLSL 上のサイズ」から取る
        // （C++ 側はパディングを持たないので sizeof の方が小さい）
        UINT dissolveSize = (static_cast<UINT>(Cb::HlslSizeOf(kDissolveParamsFields)) + 255) & ~255;
        dissolveParamsCB_ = ResourceFactory::CreateBufferResource(graphicsCore_->GetDevice(), dissolveSize);
        [[maybe_unused]] HRESULT hr = dissolveParamsCB_->Map(0, nullptr, &mappedDissolveParams_);
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();


        // ノイズテクスチャ読み込み
        auto& textureManager = TextureManager::GetInstance();
        auto texture = textureManager.Load("noise0.png");
        noiseTextureHandle_ = texture.gpuHandle;
    }

    void Dissolve::UpdateConstantBuffer()
    {
        if (!mappedDissolveParams_) {
            return;
        }

        DissolveParams params{};
        params.threshold = cvThreshold.Get();
        params.edgeWidth = cvEdgeWidth.Get();

        const Vector3& edgeColor = cvEdgeColor.Get();
        params.edgeColorR = edgeColor.x;
        params.edgeColorG = edgeColor.y;
        params.edgeColorB = edgeColor.z;

        // フィールド表を見て HLSL のオフセットへ配置する
        Cb::Upload(mappedDissolveParams_, params, kDissolveParamsFields);
    }

    void Dissolve::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateConstantBuffer();
        UpdateScreenSizeConstants(width, height);

        auto* cmdList = graphicsCore_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int inputTextureIdx  = GetRootParamIndex("inputTexture");
        int noiseTextureIdx  = GetRootParamIndex("noiseTexture");
        int outputIdx        = GetRootParamIndex("gOutput");
        int dissolveParamsIdx= GetRootParamIndex("DissolveParams");
        int screenParamsIdx  = GetRootParamIndex("ScreenParams");

        if (inputTextureIdx >= 0)   cmdList->SetComputeRootDescriptorTable(inputTextureIdx, inputSrvHandle);
        if (noiseTextureIdx >= 0)   cmdList->SetComputeRootDescriptorTable(noiseTextureIdx, noiseTextureHandle_);
        if (outputIdx >= 0)         cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (dissolveParamsIdx >= 0) cmdList->SetComputeRootConstantBufferView(dissolveParamsIdx, dissolveParamsCB_->GetGPUVirtualAddress());
        if (screenParamsIdx >= 0)   cmdList->SetComputeRootConstantBufferView(screenParamsIdx, GetScreenSizeCbAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void Dissolve::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("DissolveParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::Text("ノイズテクスチャを使用してディゾルブ効果を作成します");
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

    CVar<bool>* Dissolve::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
