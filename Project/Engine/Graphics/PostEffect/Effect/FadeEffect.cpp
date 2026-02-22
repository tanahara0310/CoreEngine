#include "FadeEffect.h"
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
#include <cassert>
#include <algorithm> // std::clampのために追加


namespace CoreEngine
{
void FadeEffect::Initialize(DirectXCommon* dxCommon)
{
    // 基底クラスの初期化
    PostEffectBase::Initialize(dxCommon);
    
    // 定数バッファの作成
    CreateConstantBuffer();
}

void FadeEffect::Update(float deltaTime)
{
    // 時間を累積
    timeAccumulator_ += deltaTime;
    params_.time = timeAccumulator_;
    
    // 定数バッファの更新
    UpdateConstantBuffer();
}

void FadeEffect::DrawImGui()
{
    ImGui::PushID("FadeEffectParams");
    
    ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
    ImGui::Text("様々なフェードエフェクトを提供します");
    ImGui::Separator();
    
    bool paramsChanged = false;
    
    // 基本設定
    if (ImGui::TreeNode("基本設定")) {
        // フェード強度調整
        if (ImGui::SliderFloat("フェード透明度", &params_.fadeAlpha, 0.0f, 1.0f, "%.3f")) {
            paramsChanged = true;
        }
        
        // フェードタイプ選択
        const char* fadeTypeNames[] = { 
            "黒フェード", "白フェード", "渦巻きフェード", 
            "波紋フェード", "グリッチフェード", "ポータルフェード" 
        };
        int currentType = static_cast<int>(params_.fadeType);
        if (ImGui::Combo("フェードタイプ", &currentType, fadeTypeNames, IM_ARRAYSIZE(fadeTypeNames))) {
            params_.fadeType = static_cast<float>(currentType);
            paramsChanged = true;
        }
        
        ImGui::TreePop();
    }
    
    // 特殊フェードのパラメータ（基本フェード以外の場合）
    int currentType = static_cast<int>(params_.fadeType);
    if (currentType >= 2) {
        if (ImGui::TreeNode("特殊パラメータ")) {
            // 渦巻きパラメータ
            if (currentType == 2) { // Spiral
                if (ImGui::SliderFloat("渦巻きの強さ", &params_.spiralPower, 0.0f, 20.0f, "%.2f")) {
                    paramsChanged = true;
                }
            }
            
            // 波紋パラメータ
            if (currentType == 3) { // Ripple
                if (ImGui::SliderFloat("波紋の周波数", &params_.rippleFreq, 1.0f, 50.0f, "%.2f")) {
                    paramsChanged = true;
                }
            }
            
            // グリッチパラメータ
            if (currentType == 4) { // Glitch
                if (ImGui::SliderFloat("グリッチ強度", &params_.glitchIntensity, 0.0f, 2.0f, "%.3f")) {
                    paramsChanged = true;
                }
            }
            
            // ポータルパラメータ
            if (currentType == 5) { // Portal
                if (ImGui::SliderFloat("ポータルサイズ", &params_.portalSize, 0.1f, 1.0f, "%.3f")) {
                    paramsChanged = true;
                }
            }
            
            // 共通パラメータ（特殊フェード用）
            if (ImGui::SliderFloat("色のシフト", &params_.colorShift, 0.0f, 6.28f, "%.3f")) {
                paramsChanged = true;
            }
            
            ImGui::TreePop();
        }
    }
    
    // パラメータが変更された場合、即座に定数バッファを更新
    if (paramsChanged) {
        UpdateConstantBuffer();
    }
    
    ImGui::Separator();
    
    if (ImGui::Button("デフォルトに戻す")) {
        params_.fadeAlpha = 0.0f;
        params_.fadeType = 0.0f;
        params_.spiralPower = 5.0f;
        params_.rippleFreq = 10.0f;
        params_.glitchIntensity = 0.5f;
        params_.portalSize = 0.3f;
        params_.colorShift = 0.0f;
        UpdateConstantBuffer();
    }
    
    if (!IsEnabled()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
    }
    
    ImGui::PopID();
}

void FadeEffect::SetFadeAlpha(float alpha)
{
    params_.fadeAlpha = std::clamp(alpha, 0.0f, 1.0f);
    UpdateConstantBuffer();
}

void FadeEffect::SetFadeType(bool fadeToBlack)
{
    params_.fadeType = fadeToBlack ? 0.0f : 1.0f;
    UpdateConstantBuffer();
}

void FadeEffect::SetFadeType(FadeType type)
{
    params_.fadeType = static_cast<float>(type);
    UpdateConstantBuffer();
}

void FadeEffect::SetSpiralPower(float power)
{
    params_.spiralPower = std::clamp(power, 0.0f, 20.0f);
    UpdateConstantBuffer();
}

void FadeEffect::SetRippleFrequency(float frequency)
{
    params_.rippleFreq = std::clamp(frequency, 1.0f, 50.0f);
    UpdateConstantBuffer();
}

void FadeEffect::SetGlitchIntensity(float intensity)
{
    params_.glitchIntensity = std::clamp(intensity, 0.0f, 2.0f);
    UpdateConstantBuffer();
}

void FadeEffect::SetPortalSize(float size)
{
    params_.portalSize = std::clamp(size, 0.1f, 1.0f);
    UpdateConstantBuffer();
}

void FadeEffect::SetColorShift(float shift)
{
    params_.colorShift = shift;
    UpdateConstantBuffer();
}

void FadeEffect::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
{
    // 定数バッファをピクセルシェーダーにバインド（シェーダーリフレクションからインデックスを取得）
    int paramsIdx = GetRootParamIndex("FadeParams");
    if (constantBuffer_ && paramsIdx >= 0) {
        commandList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
    }
}

void FadeEffect::UpdateConstantBuffer()
{
    // 定数バッファにデータをコピー
    if (mappedData_) {
        *mappedData_ = params_;
    }
}

void FadeEffect::CreateConstantBuffer()
{
    assert(directXCommon_);
    
    // 定数バッファのサイズを256バイトアライメントに調整
    UINT bufferSize = (sizeof(FadeParams) + 255) & ~255;
    
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
