#include "RadialBlur.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"

#ifdef USE_IMGUI
#include "Utility/Debug/ImGui/ImguiManager.h"
#endif
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
        static const std::wstring pixelShaderPath = L"RadialBlur.PS.hlsl";
        return pixelShaderPath;
    }

    void RadialBlur::DrawImGui()
    {
#ifdef USE_IMGUI


        ImGui::PushID(this);

        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::Text("中心から外側に向かってブラーをかけます");
        UI::Separator();

        bool paramsChanged = false;

        // パラメータ設定
        if (ImGui::TreeNode("パラメータ")) {
            // ブラー強度の調整
            if (UI::SliderFloat("強度", params_.intensity, 0.0f, 2.0f, "%.2f")) {
                paramsChanged = true;
            }

            // サンプル数の調整
            if (UI::SliderFloat("サンプル数", params_.sampleCount, 4.0f, 16.0f, "%.0f")) {
                paramsChanged = true;
            }

            ImGui::TreePop();
        }

        // 中心位置設定
        if (ImGui::TreeNode("中心位置")) {
            // ブラー中心の調整
            if (UI::SliderFloat("中心X", params_.centerX, 0.0f, 1.0f, "%.3f")) {
                paramsChanged = true;
            }

            if (UI::SliderFloat("中心Y", params_.centerY, 0.0f, 1.0f, "%.3f")) {
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
#endif // USE_IMGUI
    }

    void RadialBlur::SetParams(const RadialBlurParams& newParams)
    {
        params_ = newParams;
        UpdateConstantBuffer(); // パラメータ変更時に自動的にGPU転送
    }

    void RadialBlur::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
    {
        // 定数バッファをピクセルシェーダーにバインド（シェーダーリフレクションからインデックスを取得）
        int paramsIdx = GetRootParamIndex("RadialBlurParams");
        if (constantBuffer_ && paramsIdx >= 0) {
            commandList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
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

        // 定数バッファリソースを生成
        constantBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), bufferSize);

        // マッピング
        HRESULT hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
        assert(SUCCEEDED(hr));

        // 初期値で更新
        UpdateConstantBuffer();
    }
}
