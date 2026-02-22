#include "Sepia.h"
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
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
#ifdef _DEBUG
    ImGui::PushID("SepiaParams");
    
    ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
    ImGui::Text("ヴィンテージなセピアトーンエフェクトを作成します");
    ImGui::Separator();
    
    bool paramsChanged = false;
    
    // パラメータ設定
    if (ImGui::TreeNode("パラメータ")) {
        // セピア効果の強度調整
        paramsChanged |= ImGui::SliderFloat("強度", &params_.intensity, 0.0f, 2.0f);
        
        ImGui::TreePop();
    }
    
    // 色調調整
    if (ImGui::TreeNode("色調調整")) {
        paramsChanged |= ImGui::SliderFloat("赤の色調", &params_.toneRed, 0.5f, 1.5f);
        paramsChanged |= ImGui::SliderFloat("緑の色調", &params_.toneGreen, 0.5f, 1.5f);
        paramsChanged |= ImGui::SliderFloat("青の色調", &params_.toneBlue, 0.5f, 1.5f);
        
        ImGui::TreePop();
    }
    
    // パラメータが変更された場合、即座に定数バッファを更新
    if (paramsChanged) {
        UpdateConstantBuffer();
    }
    
    ImGui::Separator();
    
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
        
        ImGui::SameLine();
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
    
    ImGui::Separator();
    
    ImGui::PopID();
#endif // _DEBUG
}

void Sepia::SetParams(const SepiaParams& params)
{
    params_ = params;
    UpdateConstantBuffer();
}

void Sepia::ForceUpdateConstantBuffer()
{
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
