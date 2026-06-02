#include "pch.h"
#include "Outline.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>


namespace CoreEngine
{
    void Outline::OnCreateConstantBuffers()
    {
        // アウトラインパラメータ用定数バッファ
        UINT outlineSize = (sizeof(OutlineParams) + 255) & ~255;
        outlineParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), outlineSize);
        HRESULT hr = outlineParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedOutlineParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        // 画面サイズ用定数バッファ
        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void Outline::UpdateConstantBuffer()
    {
        if (mappedOutlineParams_) { *mappedOutlineParams_ = params_; }
    }

    void Outline::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void Outline::SetParams(const OutlineParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void Outline::SetCameraClipPlanes(float nearPlane, float farPlane)
    {
        // near/farが変わった場合のみ更新してGPU転送コストを抑える
        if (params_.nearPlane != nearPlane || params_.farPlane != farPlane) {
            params_.nearPlane = nearPlane;
            params_.farPlane  = farPlane;
            UpdateConstantBuffer();
        }
    }

    void Outline::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateScreenConstantBuffer(width, height);

        auto* cmdList = directXCommon_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        // シェーダーリソース名からルートパラメータインデックスを取得
        int textureIdx       = GetRootParamIndex("gTexture");
        int depthIdx         = GetRootParamIndex("gDepth");
        int outputIdx        = GetRootParamIndex("gOutput");
        int outlineParamsIdx = GetRootParamIndex("OutlineParams");
        int screenParamsIdx  = GetRootParamIndex("ScreenParams");

        // カラーテクスチャ (t0)
        if (textureIdx >= 0) {
            cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        }
        // 深度テクスチャ (t1) - 深度ステンシルバッファのSRV
        if (depthIdx >= 0) {
            cmdList->SetComputeRootDescriptorTable(depthIdx, directXCommon_->GetDepthStencilSRV());
        }
        // 出力テクスチャ (u0)
        if (outputIdx >= 0) {
            cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        }
        // アウトラインパラメータ (b0)
        if (outlineParamsIdx >= 0) {
            cmdList->SetComputeRootConstantBufferView(outlineParamsIdx, outlineParamsCB_->GetGPUVirtualAddress());
        }
        // 画面サイズパラメータ (b1)
        if (screenParamsIdx >= 0) {
            cmdList->SetComputeRootConstantBufferView(screenParamsIdx, screenParamsCB_->GetGPUVirtualAddress());
        }

        // ディスパッチ（8x8スレッドグループ）
        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void Outline::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("OutlineParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        bool changed = false;
        if (ImGui::TreeNode("パラメータ")) {
            // アウトライン色
            changed |= ImGui::ColorEdit4("アウトライン色", params_.outlineColor);

            // 深度閾値（線形深度の差分量、単位：m）
            changed |= UI::SliderFloat("深度閾値 (m)", params_.depthThreshold, 0.01f, 10.0f);

            // エッジ強度
            changed |= UI::SliderFloat("エッジ強度", params_.depthStrength, 0.1f, 20.0f);

            // アウトライン太さ
            changed |= UI::SliderFloat("アウトライン太さ", params_.outlineWidth, 1.0f, 4.0f);

            // カメラクリップ距離（シェーダーの線形化に使用）
            changed |= UI::SliderFloat("ニアクリップ", params_.nearPlane, 0.01f, 10.0f);
            changed |= UI::SliderFloat("ファークリップ", params_.farPlane, 10.0f, 5000.0f);

            ImGui::TreePop();
        }
        if (changed) { UpdateConstantBuffer(); }

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            params_ = OutlineParams{};
            UpdateConstantBuffer();
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }
}
