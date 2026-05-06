#include "RasterScroll.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
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
        static const std::wstring path = L"RasterScroll.PS.hlsl";
        return path;
    }

    void RasterScroll::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("RasterScrollParams");

        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::Text("波のような歪みエフェクトを作成します");
        UI::Separator();

        bool paramsChanged = false;

        // パラメータ設定
        if (ImGui::TreeNode("パラメータ")) {
            // スクロール速度の調整
            paramsChanged |= UI::SliderFloat("波の速度", params_.scrollSpeed, 0.0f, 10.0f);

            // ライン高さの調整
            paramsChanged |= UI::SliderFloat("波の密度", params_.lineHeight, 1.0f, 20.0f);

            // 振幅の調整
            paramsChanged |= UI::SliderFloat("波の振幅", params_.amplitude, 0.0f, 0.2f);

            // 周波数の調整
            paramsChanged |= UI::SliderFloat("波の周波数", params_.frequency, 0.1f, 5.0f);

            // ライン開始位置オフセットの調整
            paramsChanged |= UI::SliderFloat("位相オフセット", params_.lineOffset, 0.0f, 1.0f);

            // 歪み強度の調整
            paramsChanged |= UI::SliderFloat("歪みの強さ", params_.distortionStrength, 0.0f, 3.0f);

            ImGui::TreePop();
        }

        // パラメータが変更された場合、即座に定数バッファを更新
        if (paramsChanged) {
            UpdateConstantBuffer();
        }

        UI::Separator();

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
            UI::SameLine();

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
            UI::SameLine();

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

        UI::Separator();

        // 時間情報の表示
        ImGui::Text("アニメーション時間: %.2f", accumulatedTime_);

        if (!IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
        }

        ImGui::PopID();
#endif // USE_IMGUI
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

        // 定数バッファリソースを生成
        constantBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), bufferSize);

        // マッピング
        HRESULT hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
        assert(SUCCEEDED(hr));

        // 初期値で更新
        UpdateConstantBuffer();
    }
}
