#include "Bloom.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include <cassert>


namespace CoreEngine
{
    void Bloom::Initialize(DirectXCommon* dxCommon)
    {
        // 基底クラスの初期化
        PostEffectBase::Initialize(dxCommon);

        // 定数バッファの作成
        CreateConstantBuffer();
    }

    const std::wstring& Bloom::GetPixelShaderPath() const
    {
        static const std::wstring path = L"Bloom.PS.hlsl";
        return path;
    }

    void Bloom::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("BloomParams");

        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        bool paramsChanged = false;

        // パラメータ設定
        if (ImGui::TreeNode("パラメータ")) {
            // 輝度閾値の調整
            if (UI::SliderFloat("輝度しきい値", params_.threshold, 0.0f, 2.0f)) {
                paramsChanged = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("この値より明るいピクセルがブルームの対象になります");
            }

            // ブルーム強度の調整
            if (UI::SliderFloat("強度", params_.intensity, 0.0f, 3.0f)) {
                paramsChanged = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("ブルーム効果の強さを調整します");
            }

            // ブラー半径の調整
            if (UI::SliderFloat("ブラー半径", params_.blurRadius, 0.5f, 5.0f)) {
                paramsChanged = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("光の広がりの範囲を調整します");
            }

            // ソフトニーの調整
            if (UI::SliderFloat("ソフトニー", params_.softKnee, 0.0f, 1.0f)) {
                paramsChanged = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("閾値付近の滑らかさを調整します (0=ハード, 1=ソフト)");
            }

            ImGui::TreePop();
        }

        // パラメータが変更された場合、即座に定数バッファを更新
        if (paramsChanged) {
            UpdateConstantBuffer();
        }

        UI::Separator();

        if (ImGui::Button("デフォルトに戻す")) {
            params_.threshold = 0.8f;
            params_.intensity = 1.0f;
            params_.blurRadius = 2.0f;
            params_.softKnee = 0.5f;
            UpdateConstantBuffer();
        }

        if (!IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
        }

        ImGui::PopID();
#endif // USE_IMGUI
    }

    void Bloom::SetParams(const BloomParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void Bloom::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
    {
        // 定数バッファをピクセルシェーダーにバインド（シェーダーリフレクションからインデックスを取得）
        int paramsIdx = GetRootParamIndex("BloomParams");
        if (constantBuffer_ && paramsIdx >= 0) {
            commandList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
        }
    }

    void Bloom::UpdateConstantBuffer()
    {
        // 定数バッファにデータをコピー
        if (mappedData_) {
            *mappedData_ = params_;
        }
    }

    void Bloom::CreateConstantBuffer()
    {
        assert(directXCommon_);

        // 定数バッファのサイズを256バイトアライメントに調整
        UINT bufferSize = (sizeof(BloomParams) + 255) & ~255;

        // 定数バッファリソースを生成
        constantBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), bufferSize);

        // マッピング
        HRESULT hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
        assert(SUCCEEDED(hr));

        // 初期値で更新
        UpdateConstantBuffer();
    }
}
