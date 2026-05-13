#include "Sepia.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include <cassert>


namespace CoreEngine
{
void Sepia::Initialize(DirectXCommon* dxCommon)
{
    // 基底クラスの初期化
    PostEffectBase::Initialize(dxCommon);
    
    // 定数バッファの作成
    CreateConstantBuffer();
}

void Sepia::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::PushID("SepiaParams");
    
    ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
    ImGui::Text("ヴィンテージなセピアトーンエフェクトを作成します");
    UI::Separator();
    
    bool paramsChanged = false;
    
    // パラメータ設定
    if (ImGui::TreeNode("パラメータ")) {
        // セピア効果の強度調整
        paramsChanged |= UI::SliderFloat("強度", params_.intensity, 0.0f, 2.0f);
        
        ImGui::TreePop();
    }
    
    // 色調調整
    if (ImGui::TreeNode("色調調整")) {
        paramsChanged |= UI::SliderFloat("赤の色調", params_.toneRed, 0.5f, 1.5f);
        paramsChanged |= UI::SliderFloat("緑の色調", params_.toneGreen, 0.5f, 1.5f);
        paramsChanged |= UI::SliderFloat("青の色調", params_.toneBlue, 0.5f, 1.5f);
        
        ImGui::TreePop();
    }
    
    // パラメータが変更された場合、即座に定数バッファを更新
    if (paramsChanged) {
        UpdateConstantBuffer();
    }
    
    UI::Separator();
    
    // プリセット
    if (ImGui::TreeNode("プリセット")) {
        if (ImGui::Button("デフォルト")) {
            params_.intensity = 1.0f;
            params_.toneRed = 1.0f;
            params_.toneGreen = 0.8f;
            params_.toneBlue = 0.6f;
            UpdateConstantBuffer();
        }
        
        if (ImGui::Button("クラシックセピア")) {
            params_.intensity = 1.2f;
            params_.toneRed = 1.1f;
            params_.toneGreen = 0.85f;
            params_.toneBlue = 0.65f;
            UpdateConstantBuffer();
        }
        
        UI::SameLine();
        if (ImGui::Button("暖色セピア")) {
            params_.intensity = 0.8f;
            params_.toneRed = 1.3f;
            params_.toneGreen = 0.9f;
            params_.toneBlue = 0.5f;
            UpdateConstantBuffer();
        }
        
        ImGui::TreePop();
    }
    
    if (!IsEnabled()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
    }
    
    UI::Separator();
    
    ImGui::PopID();
#endif // USE_IMGUI
}

void Sepia::SetParams(const SepiaParams& params)
{
    params_ = params;
    UpdateConstantBuffer();
}

void Sepia::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
{
    // 定数バッファをピクセルシェーダーにバインド（シェーダーリフレクションからインデックスを取得）
    int paramsIdx = GetRootParamIndex("SepiaParams");
    if (constantBuffer_ && paramsIdx >= 0) {
        commandList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
    }
}

void Sepia::UpdateConstantBuffer()
{
    // 定数バッファにデータをコピー
    if (mappedData_) {
        *mappedData_ = params_;
    }
}

void Sepia::CreateConstantBuffer()
{
    assert(directXCommon_);
    
    // 定数バッファのサイズを256バイトアライメントに調整
    UINT bufferSize = (sizeof(SepiaParams) + 255) & ~255;

    // 定数バッファリソースを生成
    constantBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), bufferSize);

    // マッピング
    HRESULT hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
    assert(SUCCEEDED(hr));
    
    // 初期値で更新
    UpdateConstantBuffer();
}
}
