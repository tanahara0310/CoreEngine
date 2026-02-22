#include "RasterScroll.h"
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
#include <cassert>


namespace CoreEngine
{
void RasterScroll::Initialize(DirectXCommon* dxCommon)
{
    // 基底クラスの初期化
    PostEffectBase::Initialize(dxCommon);
    
    // 定数バッファの作成
    CreateConstantBuffer();
}

void RasterScroll::Update(float deltaTime)
{
    // 時間を累積
    accumulatedTime_ += deltaTime;
    
    // パラメータに時間を設定
    params_.time = accumulatedTime_;
    
    // 定数バッファを更新
    UpdateConstantBuffer();
}

const std::wstring& RasterScroll::GetPixelShaderPath() const
{
    static const std::wstring path = L"Assets/Shaders/PostProcess/RasterScroll.PS.hlsl";
    return path;
}

void RasterScroll::DrawImGui()
{
#ifdef _DEBUG
    ImGui::PushID("RasterScrollParams");
    
    ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
    ImGui::Text("波のような歪みエフェクトを作成します");
    ImGui::Separator();
    
    bool paramsChanged = false;
    
    // パラメータ設定
    if (ImGui::TreeNode("パラメータ")) {
        // スクロール速度の調整
        paramsChanged |= ImGui::SliderFloat("波の速度", &params_.scrollSpeed, 0.0f, 10.0f);
        
        // ライン高さの調整
        paramsChanged |= ImGui::SliderFloat("波の密度", &params_.lineHeight, 1.0f, 20.0f);
        
        // 振幅の調整
        paramsChanged |= ImGui::SliderFloat("波の振幅", &params_.amplitude, 0.0f, 0.2f);
        
        // 周波数の調整
        paramsChanged |= ImGui::SliderFloat("波の周波数", &params_.frequency, 0.1f, 5.0f);
        
        // ライン開始位置オフセットの調整
        paramsChanged |= ImGui::SliderFloat("位相オフセット", &params_.lineOffset, 0.0f, 1.0f);
        
        // 歪み強度の調整
        paramsChanged |= ImGui::SliderFloat("歪みの強さ", &params_.distortionStrength, 0.0f, 3.0f);
        
        ImGui::TreePop();
    }
    
    // パラメータが変更された場合、即座に定数バッファを更新
    if (paramsChanged) {
        UpdateConstantBuffer();
    }
    
    ImGui::Separator();
    
    // プリセットボタン
    if (ImGui::TreeNode("プリセット")) {
        if (ImGui::Button("微妙な波")) {
            params_.scrollSpeed = 0.8f;
            params_.lineHeight = 10.0f;
            params_.amplitude = 0.01f;
            params_.frequency = 1.5f;
            params_.lineOffset = 0.0f;
            params_.distortionStrength = 0.8f;
            UpdateConstantBuffer();
        }
        ImGui::SameLine();
        
        if (ImGui::Button("海の波")) {
            params_.scrollSpeed = 1.5f;
            params_.lineHeight = 8.0f;
            params_.amplitude = 0.03f;
            params_.frequency = 2.0f;
            params_.lineOffset = 0.0f;
            params_.distortionStrength = 1.2f;
            UpdateConstantBuffer();
        }
        
        if (ImGui::Button("強い歪み")) {
            params_.scrollSpeed = 2.5f;
            params_.lineHeight = 12.0f;
            params_.amplitude = 0.08f;
            params_.frequency = 1.0f;
            params_.lineOffset = 0.0f;
            params_.distortionStrength = 2.0f;
            UpdateConstantBuffer();
        }
        ImGui::SameLine();
        
        if (ImGui::Button("熱の揺らぎ")) {
            params_.scrollSpeed = 3.0f;
            params_.lineHeight = 15.0f;
            params_.amplitude = 0.02f;
            params_.frequency = 3.0f;
            params_.lineOffset = 0.0f;
            params_.distortionStrength = 1.5f;
            UpdateConstantBuffer();
        }
        
        if (ImGui::Button("デフォルトに戻す")) {
            params_.scrollSpeed = 1.0f;
            params_.lineHeight = 10.0f;
            params_.amplitude = 0.02f;
            params_.frequency = 1.5f;
            params_.lineOffset = 0.0f;
            params_.distortionStrength = 1.0f;
            UpdateConstantBuffer();
        }
        
        ImGui::TreePop();
    }
    
    ImGui::Separator();
    
    // 時間情報の表示
    ImGui::Text("アニメーション時間: %.2f", accumulatedTime_);
    
    if (!IsEnabled()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
    }
    
    ImGui::PopID();
#endif // _DEBUG
}

void RasterScroll::SetParams(const RasterScrollParams& params)
{
    params_ = params;
    UpdateConstantBuffer();
}

void RasterScroll::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
{
    // 定数バッファをピクセルシェーダーにバインド（シェーダーリフレクションからインデックスを取得）
    int paramsIdx = GetRootParamIndex("RasterScrollParams");
    if (constantBuffer_ && paramsIdx >= 0) {
        commandList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
    }
}

void RasterScroll::UpdateConstantBuffer()
{
    // 定数バッファにデータをコピー
    if (mappedData_) {
        *mappedData_ = params_;
    }
}

void RasterScroll::CreateConstantBuffer()
{
    assert(directXCommon_);
    
    // 定数バッファのサイズを256バイトアライメントに調整
    UINT bufferSize = (sizeof(RasterScrollParams) + 255) & ~255;
    
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
