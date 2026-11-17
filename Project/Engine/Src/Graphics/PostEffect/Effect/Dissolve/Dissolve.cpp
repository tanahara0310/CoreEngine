#include "pch.h"
#include "Dissolve.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/CVar/CVar.h"
#ifdef CORE_EDITOR
#include "Editor/ImGui/CVarPanel.h"
#endif


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

    void Dissolve::OnCreateResources()
    {
        // ノイズテクスチャ読み込み
        auto& textureManager = TextureManager::GetInstance();
        auto texture = textureManager.Load("noise0.png", TextureColorSpace::Linear);
        noiseTextureHandle_ = texture.gpuHandle;
    }

    Dissolve::DissolveParams Dissolve::MakeParams() const
    {
        DissolveParams params{};
        params.threshold = cvThreshold.Get();
        params.edgeWidth = cvEdgeWidth.Get();

        const Vector3& edgeColor = cvEdgeColor.Get();
        params.edgeColorR = edgeColor.x;
        params.edgeColorG = edgeColor.y;
        params.edgeColorB = edgeColor.z;
        return params;
    }

    void Dissolve::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateScreenSizeConstants(width, height);

        // DissolveParams はフィールド表を見て HLSL のオフセットへ配置する
        DispatchPass(graphicsCore_->GetCommandList(),
            { { "inputTexture", inputSrvHandle }, { "noiseTexture", noiseTextureHandle_ }, { "gOutput", outputUavHandle } },
            { { "DissolveParams", UploadConstants(MakeParams(), kDissolveParamsFields) },
              { "ScreenParams", GetScreenSizeCbAddress() } },
            width, height);
    }

    void Dissolve::DrawImGui()
    {
#ifdef CORE_EDITOR
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
#endif // CORE_EDITOR
    }

    CVar<bool>* Dissolve::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
