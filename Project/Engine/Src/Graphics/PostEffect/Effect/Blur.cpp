#include "Blur.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include <cassert>


namespace CoreEngine
{
    void Blur::Initialize(DirectXCommon* dxCommon)
    {
        // 基底クラスの初期化
        PostEffectBase::Initialize(dxCommon);

        // 定数バッファの作成
        CreateConstantBuffer();
    }

    const std::wstring& Blur::GetPixelShaderPath() const
    {
        static const std::wstring path = L"Engine/Assets/Shaders/PostProcess/Blur.PS.hlsl";
        return path;
    }

    void Blur::DrawImGui()
    {
#ifdef _DEBUG
        ImGui::PushID("BlurParams");

        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::Separator();

        bool paramsChanged = false;

        // パラメータ設定
        if (ImGui::TreeNode("パラメータ")) {
            // ブラー強度の調整
            paramsChanged |= ImGui::SliderFloat("強度", &params_.intensity, 0.0f, 5.0f);

            // カーネルサイズの調整
            paramsChanged |= ImGui::SliderFloat("カーネルサイズ", &params_.kernelSize, 0.5f, 3.0f);

            ImGui::TreePop();
        }

        // パラメータが変更された場合、即座に定数バッファを更新
        if (paramsChanged) {
            UpdateConstantBuffer();
        }

        ImGui::Separator();

        if (ImGui::Button("デフォルトに戻す")) {
            params_.intensity = 1.0f;
            params_.kernelSize = 1.0f;
            UpdateConstantBuffer();
        }

        if (!IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
        }

        ImGui::PopID();
#endif // _DEBUG
    }

    void Blur::SetParams(const BlurParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void Blur::ForceUpdateConstantBuffer()
    {
        UpdateConstantBuffer();
    }

    void Blur::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
    {
        // 定数バッファをピクセルシェーダーにバインド（シェーダーリフレクションからインデックスを取得）
        int paramsIdx = GetRootParamIndex("BlurParams");
        if (constantBuffer_ && paramsIdx >= 0) {
            commandList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
        }
    }

    void Blur::UpdateConstantBuffer()
    {
        // 定数バッファにデータをコピー
        if (mappedData_) {
            *mappedData_ = params_;
        }
    }

    void Blur::CreateConstantBuffer()
    {
        assert(directXCommon_);

        // 定数バッファのサイズを256バイトアライメントに調整
        UINT bufferSize = (sizeof(BlurParams) + 255) & ~255;

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
