#include "ColorGrading.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include <cassert>


namespace CoreEngine
{
void ColorGrading::Initialize(DirectXCommon* dxCommon)
{
    // 基底クラスの初期化
    PostEffectBase::Initialize(dxCommon);
    
    // 定数バッファの作成
    CreateConstantBuffer();
}

void ColorGrading::DrawImGui()
{
    ImGui::PushID("ColorGradingParams");
    
    ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
    ImGui::Text("色のバランス、コントラスト、全体的なムードを調整します");
    ImGui::Separator();
    
    bool paramsChanged = false;
    
    // 基本カラー調整
    if (ImGui::TreeNode("基本カラー調整")) {
        paramsChanged |= ImGui::SliderFloat("色相", &params_.hue, -1.0f, 1.0f, "%.2f");
        paramsChanged |= ImGui::SliderFloat("彩度", &params_.saturation, 0.0f, 3.0f, "%.2f");
        paramsChanged |= ImGui::SliderFloat("明度", &params_.value, 0.0f, 3.0f, "%.2f");
        paramsChanged |= ImGui::SliderFloat("コントラスト", &params_.contrast, 0.0f, 3.0f, "%.2f");
        
        ImGui::TreePop();
    }
    
    // 露出とガンマ
    if (ImGui::TreeNode("露出とガンマ")) {
        paramsChanged |= ImGui::SliderFloat("露出", &params_.exposure, -3.0f, 3.0f, "%.2f");
        paramsChanged |= ImGui::SliderFloat("ガンマ", &params_.gamma, 0.1f, 3.0f, "%.2f");
        
        ImGui::TreePop();
    }
    
    // 色温度調整
    if (ImGui::TreeNode("ホワイトバランス")) {
        paramsChanged |= ImGui::SliderFloat("色温度", &params_.temperature, -1.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        ImGui::TextDisabled("(-)寒色 - 暖色(+)");
        
        paramsChanged |= ImGui::SliderFloat("色合い", &params_.tint, -1.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        ImGui::TextDisabled("(-)緑 - マゼンタ(+)");
        
        ImGui::TreePop();
    }
    
    // Shadow/Midtone/Highlight調整
    if (ImGui::TreeNode("シャドウ/ミッドトーン/ハイライト")) {
        ImGui::Text("シャドウリフト (RGB):");
        paramsChanged |= ImGui::SliderFloat3("##ShadowLift", params_.shadowLift, -1.0f, 1.0f, "%.3f");
        
        ImGui::Text("ミッドトーンガンマ (RGB):");
        paramsChanged |= ImGui::SliderFloat3("##MidtoneGamma", params_.midtoneGamma, 0.1f, 3.0f, "%.3f");
        
        ImGui::Text("ハイライトゲイン (RGB):");
        paramsChanged |= ImGui::SliderFloat3("##HighlightGain", params_.highlightGain, 0.0f, 3.0f, "%.3f");
        
        ImGui::TreePop();
    }
    
    // プリセット
    if (ImGui::TreeNode("プリセット")) {
        if (ImGui::Button("ニュートラル")) {
            ApplyPreset(0);
            paramsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("暖色")) {
            ApplyPreset(1);
            paramsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("寒色")) {
            ApplyPreset(2);
            paramsChanged = true;
        }
        
        if (ImGui::Button("シネマティック")) {
            ApplyPreset(3);
            paramsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("ハイコントラスト")) {
            ApplyPreset(4);
            paramsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("ヴィンテージ")) {
            ApplyPreset(5);
            paramsChanged = true;
        }
        
        ImGui::TreePop();
    }
    
    // パラメータが変更された場合、即座に定数バッファを更新
    if (paramsChanged) {
        UpdateConstantBuffer();
    }
    
    ImGui::Separator();
    
    if (ImGui::Button("デフォルトに戻す")) {
        ApplyPreset(0); // ニュートラルプリセット
        UpdateConstantBuffer();
    }
    
    if (!IsEnabled()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
    }
    
    ImGui::Separator();
    
    ImGui::PopID();
}

void ColorGrading::ApplyPreset(int presetIndex)
{
    switch (presetIndex) {
    case 0: // Neutral
        params_.hue = 0.0f;
        params_.saturation = 1.0f;
        params_.value = 1.0f;
        params_.contrast = 1.0f;
        params_.gamma = 1.0f;
        params_.temperature = 0.0f;
        params_.tint = 0.0f;
        params_.exposure = 0.0f;
        for (int i = 0; i < 3; ++i) {
            params_.shadowLift[i] = 0.0f;
            params_.midtoneGamma[i] = 1.0f;
            params_.highlightGain[i] = 1.0f;
        }
        break;
        
    case 1: // Warm
        params_.hue = 0.05f;
        params_.saturation = 1.1f;
        params_.value = 1.05f;
        params_.contrast = 1.1f;
        params_.gamma = 0.95f;
        params_.temperature = 0.3f;
        params_.tint = -0.1f;
        params_.exposure = 0.1f;
        break;
        
    case 2: // Cool
        params_.hue = -0.05f;
        params_.saturation = 1.05f;
        params_.value = 0.98f;
        params_.contrast = 1.05f;
        params_.gamma = 1.05f;
        params_.temperature = -0.3f;
        params_.tint = 0.1f;
        params_.exposure = -0.05f;
        break;
        
    case 3: // Cinematic
        params_.hue = 0.02f;
        params_.saturation = 1.2f;
        params_.value = 0.95f;
        params_.contrast = 1.3f;
        params_.gamma = 0.9f;
        params_.temperature = 0.1f;
        params_.tint = -0.05f;
        params_.exposure = 0.0f;
        // シネマティックな色調整
        params_.shadowLift[0] = 0.1f; // R
        params_.shadowLift[1] = 0.05f; // G
        params_.shadowLift[2] = 0.0f; // B
        params_.highlightGain[0] = 0.95f; // R
        params_.highlightGain[1] = 1.0f; // G
        params_.highlightGain[2] = 1.1f; // B
        break;
        
    case 4: // High Contrast
        params_.hue = 0.0f;
        params_.saturation = 1.3f;
        params_.value = 1.0f;
        params_.contrast = 1.5f;
        params_.gamma = 1.0f;
        params_.temperature = 0.0f;
        params_.tint = 0.0f;
        params_.exposure = 0.0f;
        break;
        
    case 5: // Vintage
        params_.hue = 0.08f;
        params_.saturation = 0.8f;
        params_.value = 0.9f;
        params_.contrast = 1.2f;
        params_.gamma = 1.1f;
        params_.temperature = 0.4f;
        params_.tint = -0.2f;
        params_.exposure = -0.1f;
        // ヴィンテージ調
        params_.shadowLift[0] = 0.15f; // R
        params_.shadowLift[1] = 0.1f; // G
        params_.shadowLift[2] = 0.05f; // B
        break;
    }
}

void ColorGrading::SetParams(const ColorGradingParams& params)
{
    params_ = params;
    UpdateConstantBuffer();
}

void ColorGrading::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
{
    // 定数バッファをピクセルシェーダーにバインド（シェーダーリフレクションからインデックスを取得）
    int paramsIdx = GetRootParamIndex("ColorGradingParams");
    if (constantBuffer_ && paramsIdx >= 0) {
        commandList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
    }
}

void ColorGrading::UpdateConstantBuffer()
{
    // 定数バッファにデータをコピー
    if (mappedData_) {
        *mappedData_ = params_;
    }
}

void ColorGrading::CreateConstantBuffer()
{
    assert(directXCommon_);
    
    // 定数バッファのサイズを256バイトアライメントに調整
    UINT bufferSize = (sizeof(ColorGradingParams) + 255) & ~255;
    
    // ヒーププロパティ
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    
    // リソースデスク
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = bufferSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    
    // リソースの作成
    HRESULT hr = directXCommon_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&constantBuffer_)
    );
    assert(SUCCEEDED(hr));
    
    // マッピング
    hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
    assert(SUCCEEDED(hr));
    
    // 初期値で更新
    UpdateConstantBuffer();
}
}
