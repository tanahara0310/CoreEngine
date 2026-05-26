#include "pch.h"
#include "Dissolve.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>


namespace CoreEngine
{
    void Dissolve::OnCreateConstantBuffers()
    {
        UINT dissolveSize = (sizeof(DissolveParams) + 255) & ~255;
        dissolveParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), dissolveSize);
        HRESULT hr = dissolveParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedDissolveParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));

        // ノイズテクスチャ読み込み
        auto& textureManager = TextureManager::GetInstance();
        auto texture = textureManager.Load("noise0.png");
        noiseTextureHandle_ = texture.gpuHandle;
    }

    void Dissolve::UpdateConstantBuffer()
    {
        if (mappedDissolveParams_) { *mappedDissolveParams_ = params_; }
    }

    void Dissolve::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void Dissolve::SetParams(const DissolveParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void Dissolve::SetThreshold(float threshold)
    {
        params_.threshold = threshold;
        UpdateConstantBuffer();
    }

    void Dissolve::SetEdgeWidth(float width)
    {
        params_.edgeWidth = width;
        UpdateConstantBuffer();
    }

    void Dissolve::SetEdgeColor(float r, float g, float b)
    {
        params_.edgeColorR = r;
        params_.edgeColorG = g;
        params_.edgeColorB = b;
        UpdateConstantBuffer();
    }

    void Dissolve::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateScreenConstantBuffer(width, height);

        auto* cmdList = directXCommon_->GetCommandList();
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
        if (screenParamsIdx >= 0)   cmdList->SetComputeRootConstantBufferView(screenParamsIdx, screenParamsCB_->GetGPUVirtualAddress());

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

        bool changed = false;
        if (ImGui::TreeNode("パラメータ")) {
            changed |= UI::SliderFloat("閾値", params_.threshold, 0.0f, 1.0f);
            changed |= UI::SliderFloat("エッジ幅", params_.edgeWidth, 0.0f, 0.5f);

            float edgeColor[3] = { params_.edgeColorR, params_.edgeColorG, params_.edgeColorB };
            if (ImGui::ColorEdit3("エッジカラー", edgeColor)) {
                params_.edgeColorR = edgeColor[0];
                params_.edgeColorG = edgeColor[1];
                params_.edgeColorB = edgeColor[2];
                changed = true;
            }
            ImGui::TreePop();
        }
        if (changed) { UpdateConstantBuffer(); }

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            params_ = DissolveParams{};
            UpdateConstantBuffer();
        }
        if (!IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }
}
