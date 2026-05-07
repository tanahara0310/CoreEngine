#include "SSAO.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include <cstring>
#include <cassert>

namespace CoreEngine
{
    void SSAO::Initialize(DirectXCommon* dxCommon)
    {
        PostEffectBase::Initialize(dxCommon);
        CreateConstantBuffer();
    }

    const std::wstring& SSAO::GetPixelShaderPath() const
    {
        static const std::wstring path = L"SSAO.PS.hlsl";
        return path;
    }

    void SSAO::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
    {
        // t0 は gTexture (PostEffectBase が gNormalRoughness に相当) だが
        // SSAO.PS.hlsl は t0=gNormalRoughness, t1=gWorldPosition で宣言しているため
        // gNormalRoughness は inputSrvHandle 経由でバインド済み（gTexture と同レジスタ）。
        // gWorldPosition は追加 SRV として手動バインド。
        const int wpIdx = GetRootParamIndex("gWorldPosition");
        if (wpIdx >= 0 && worldPositionHandle_.ptr != 0) {
            commandList->SetGraphicsRootDescriptorTable(wpIdx, worldPositionHandle_);
        }

        const int paramsIdx = GetRootParamIndex("SSAOParams");
        if (paramsIdx >= 0 && constantBuffer_) {
            commandList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
        }
    }

    void SSAO::SetViewMatrix(const float mat[16])
    {
        std::memcpy(params_.viewMatrix, mat, sizeof(float) * 16);
    }

    void SSAO::SetProjectionMatrix(const float mat[16])
    {
        std::memcpy(params_.projectionMatrix, mat, sizeof(float) * 16);
    }

    void SSAO::SetScreenSize(float w, float h)
    {
        params_.screenWidth  = w;
        params_.screenHeight = h;
    }

    void SSAO::SetParams(const SSAOParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void SSAO::UpdateConstantBuffer()
    {
        if (mappedData_) {
            *mappedData_ = params_;
        }
    }

    void SSAO::CreateConstantBuffer()
    {
        assert(directXCommon_);
        const UINT bufferSize = (sizeof(SSAOParams) + 255) & ~255;
        constantBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), bufferSize);
        HRESULT hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();
    }

    void SSAO::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("SSAOParams");

        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        bool changed = false;
        if (ImGui::TreeNode("パラメータ")) {
            if (UI::SliderFloat("半径", params_.radius, 0.05f, 2.0f))    { changed = true; }
            if (UI::SliderFloat("バイアス", params_.bias, 0.001f, 0.1f)) { changed = true; }
            if (UI::SliderFloat("強度", params_.intensity, 0.0f, 3.0f))  { changed = true; }
            if (UI::SliderFloat("べき乗", params_.power, 0.5f, 4.0f))    { changed = true; }
            if (ImGui::SliderInt("サンプル数", &params_.sampleCount, 4, 64)) { changed = true; }
            ImGui::TreePop();
        }

        if (changed) {
            UpdateConstantBuffer();
        }

        if (ImGui::Button("デフォルトに戻す")) {
            params_.radius      = 0.5f;
            params_.bias        = 0.025f;
            params_.intensity   = 1.0f;
            params_.power       = 1.5f;
            params_.sampleCount = 16;
            UpdateConstantBuffer();
        }

        ImGui::PopID();
#endif
    }
}
