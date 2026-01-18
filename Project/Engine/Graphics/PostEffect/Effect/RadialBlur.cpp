#include "RadialBlur.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
#include <cassert>


namespace CoreEngine
{
void RadialBlur::Initialize(DirectXCommon* dxCommon)
{
    PostEffectBase::Initialize(dxCommon);
    CreateConstantBuffer();
}

const std::wstring& RadialBlur::GetPixelShaderPath() const
{
    static const std::wstring pixelShaderPath = L"Assets/Shaders/PostProcess/RadialBlur.PS.hlsl";
    return pixelShaderPath;
}

void RadialBlur::DrawImGui()
{
    ImGui::PushID(this);
    
    ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
    ImGui::Text("中心から外側に向かってブラーをかけます");
    ImGui::Separator();
    
    bool paramsChanged = false;
    
    // パラメータ設定
    if (ImGui::TreeNode("パラメータ")) {
        // ブラー強度の調整
        if (ImGui::SliderFloat("強度", &params_.intensity, 0.0f, 2.0f, "%.2f")) {
            paramsChanged = true;
        }
        
        // サンプル数の調整
        if (ImGui::SliderFloat("サンプル数", &params_.sampleCount, 4.0f, 16.0f, "%.0f")) {
            paramsChanged = true;
        }
        
        ImGui::TreePop();
    }
    
    // 中心位置設定
    if (ImGui::TreeNode("中心位置")) {
        // ブラー中心の調整
        if (ImGui::SliderFloat("中心X", &params_.centerX, 0.0f, 1.0f, "%.3f")) {
            paramsChanged = true;
        }
        
        if (ImGui::SliderFloat("中心Y", &params_.centerY, 0.0f, 1.0f, "%.3f")) {
            paramsChanged = true;
        }
        
        // 中心位置のリセットボタン
        if (ImGui::Button("中心をリセット")) {
            params_.centerX = 0.5f;
            params_.centerY = 0.5f;
            paramsChanged = true;
        }
        
        ImGui::TreePop();
    }
    
    // パラメータが変更された場合は定数バッファを更新
    if (paramsChanged) {
        UpdateConstantBuffer();
    }
    
    ImGui::PopID();
}

void RadialBlur::SetParams(const RadialBlurParams& newParams)
{
    params_ = newParams;
    UpdateConstantBuffer(); // パラメータ変更時に自動的にGPU転送
}

void RadialBlur::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
{
    // 定数バッファをピクセルシェーダーにバインド
    if (constantBuffer_) {
        commandList->SetGraphicsRootConstantBufferView(1, constantBuffer_->GetGPUVirtualAddress());
    }
}

void RadialBlur::UpdateConstantBuffer()
{
    // 定数バッファにデータをコピー
    if (mappedData_) {
        *mappedData_ = params_;
    }
}

void RadialBlur::CreateConstantBuffer()
{
    assert(directXCommon_);
    
    // 定数バッファのサイズを256バイトアライメントに調整
    UINT bufferSize = (sizeof(RadialBlurParams) + 255) & ~255;
    
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
