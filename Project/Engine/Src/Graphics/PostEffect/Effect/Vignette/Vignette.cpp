#include "Vignette.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include <cassert>


namespace CoreEngine
{
void Vignette::Initialize(DirectXCommon* dxCommon)
{
    // 基底クラスの初期化
    PostEffectBase::Initialize(dxCommon);
    
    // 定数バッファの作成
    CreateConstantBuffer();
}

void Vignette::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::PushID("VignetteParams");
    
    ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
    ImGui::Text("画像の端を暗くする効果を作成します");
    UI::Separator();
    
    bool paramsChanged = false;
    
    // パラメータ設定
    if (ImGui::TreeNode("パラメータ")) {
        // ヴィネット強度の調整
        paramsChanged |= UI::SliderFloat("強度", params_.intensity, 0.0f, 2.0f);
        
        // 滑らかさの調整
        paramsChanged |= UI::SliderFloat("滑らかさ", params_.smoothness, 0.1f, 2.0f);
        
        // サイズの調整
        paramsChanged |= UI::SliderFloat("サイズ", params_.size, 1.0f, 50.0f);
        
        ImGui::TreePop();
    }
    
    // パラメータが変更された場合、即座に定数バッファを更新
    if (paramsChanged) {
        UpdateConstantBuffer();
    }
    
    UI::Separator();
    
    if (ImGui::Button("デフォルトに戻す")) {
        params_.intensity = 0.8f;
        params_.smoothness = 0.8f;
        params_.size = 16.0f;
        UpdateConstantBuffer();
    }
    
    if (!IsEnabled()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
    }
    
    UI::Separator();
    
    ImGui::PopID();
#endif // USE_IMGUI
}

void Vignette::SetParams(const VignetteParams& params)
{
    params_ = params;
    UpdateConstantBuffer();
}

void Vignette::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
{
    // 定数バッファをピクセルシェーダーにバインド（シェーダーリフレクションからインデックスを取得）
    int paramsIdx = GetRootParamIndex("VignetteParams");
    if (constantBuffer_ && paramsIdx >= 0) {
        commandList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
    }
}

void Vignette::UpdateConstantBuffer()
{
    // 定数バッファにデータをコピー
    if (mappedData_) {
        *mappedData_ = params_;
    }
}

void Vignette::CreateConstantBuffer()
{
    assert(directXCommon_);
    
    // 定数バッファのサイズを256バイトアライメントに調整
    UINT bufferSize = (sizeof(VignetteParams) + 255) & ~255;

    // 定数バッファリソースを生成
    constantBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), bufferSize);

    // マッピング
    HRESULT hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
    assert(SUCCEEDED(hr));
    
    // 初期値で更新
    UpdateConstantBuffer();
}
}
