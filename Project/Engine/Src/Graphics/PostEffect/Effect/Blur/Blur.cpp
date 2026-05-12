#include "Blur.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
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
        static const std::wstring path = L"Blur.PS.hlsl";
        return path;
    }

    void Blur::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("BlurParams");

        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        bool paramsChanged = false;

        // パラメータ設定
        if (ImGui::TreeNode("パラメータ")) {
            // ブラー強度の調整
            paramsChanged |= UI::SliderFloat("強度", params_.intensity, 0.0f, 5.0f);

            // カーネルサイズの調整
            paramsChanged |= UI::SliderFloat("カーネルサイズ", params_.kernelSize, 0.5f, 3.0f);

            ImGui::TreePop();
        }

        // パラメータが変更された場合、即座に定数バッファを更新
        if (paramsChanged) {
            UpdateConstantBuffer();
        }

        UI::Separator();

        if (ImGui::Button("デフォルトに戻す")) {
            params_.intensity = 1.0f;
            params_.kernelSize = 1.0f;
            UpdateConstantBuffer();
        }

        if (!IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
        }

        ImGui::PopID();
#endif // USE_IMGUI
    }

    void Blur::SetParams(const BlurParams& params)
    {
        params_ = params;
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

        // 定数バッファリソースを生成
        constantBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), bufferSize);

        // マッピング
        HRESULT hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
        assert(SUCCEEDED(hr));

        // 初期値で更新
        UpdateConstantBuffer();
    }
}
