#include "Shockwave.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include <cassert>


namespace CoreEngine
{
    void Shockwave::Initialize(DirectXCommon* dxCommon)
    {
        // 基底クラスの初期化を呼び出す
        PostEffectBase::Initialize(dxCommon);

        // 定数バッファを作成
        CreateConstantBuffer();
    }

    void Shockwave::StartShockwave(float centerX, float centerY)
    {
        params_.center[0] = centerX;
        params_.center[1] = centerY;
        params_.time = 0.0f;
        isActive_ = true;

        // 定数バッファを即座に更新
        UpdateConstantBuffer();
    }

    void Shockwave::Update(float deltaTime)
    {
        if (!isActive_) {
            // アクティブでない場合でも、パラメータの変更を反映するために定数バッファを更新
            UpdateConstantBuffer();
            return;
        }

        params_.time += deltaTime;

        // 波が画面端に到達したら停止
        float currentRadius = params_.time * params_.speed;
        if (currentRadius > maxRadius_) {
            isActive_ = false;
            params_.time = 0.0f;
        }

        // 定数バッファにデータをコピー
        UpdateConstantBuffer();
    }

    void Shockwave::DrawImGui()
    {
        ImGui::PushID("ShockwaveParams");

        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::Text("衝撃波エフェクトを生成します");
        ImGui::Separator();

        // エフェクトが無効でもパラメータは表示・調整可能
        bool paramsChanged = false;

        // パラメータ設定
        if (ImGui::TreeNode("パラメータ")) {
            paramsChanged |= ImGui::SliderFloat2("中心位置", params_.center, 0.0f, 1.0f);
            paramsChanged |= ImGui::SliderFloat("強度", &params_.strength, 0.0f, 1.0f);
            paramsChanged |= ImGui::SliderFloat("厚さ", &params_.thickness, 0.01f, 0.5f);
            paramsChanged |= ImGui::SliderFloat("速度", &params_.speed, 0.1f, 5.0f);
            paramsChanged |= ImGui::SliderFloat("最大半径", &maxRadius_, 0.5f, 2.0f);

            ImGui::TreePop();
        }

        // パラメータが変更された場合、即座に定数バッファを更新
        if (paramsChanged) {
            UpdateConstantBuffer();
        }

        ImGui::Separator();

        if (ImGui::Button("衝撃波を発生")) {
            StartShockwave(params_.center[0], params_.center[1]);
        }

        ImGui::SameLine();

        if (ImGui::Button("リセット")) {
            params_.time = 0.0f;
            isActive_ = false;
            UpdateConstantBuffer();
        }

        ImGui::Separator();

        ImGui::Text("アクティブ: %s", isActive_ ? "はい" : "いいえ");
        ImGui::Text("現在の時間: %.2f", params_.time);

        if (!IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
        }

        ImGui::PopID();
    }

    void Shockwave::SetParams(const ShockwaveParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    const std::wstring& Shockwave::GetPixelShaderPath() const
    {
        static const std::wstring path = L"Engine/Assets/Shaders/PostProcess/Shockwave.PS.hlsl";
        return path;
    }

    void Shockwave::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
    {
        // 定数バッファをピクセルシェーダーにバインド（シェーダーリフレクションからインデックスを取得）
        int paramsIdx = GetRootParamIndex("ShockwaveParams");
        if (constantBuffer_ && paramsIdx >= 0) {
            commandList->SetGraphicsRootConstantBufferView(paramsIdx, constantBuffer_->GetGPUVirtualAddress());
        }
    }

    void Shockwave::UpdateConstantBuffer()
    {
        // 定数バッファにデータをコピー
        if (mappedData_) {
            *mappedData_ = params_;
        }
    }

    void Shockwave::CreateConstantBuffer()
    {
        assert(directXCommon_);

        // 定数バッファのサイズを256バイトアライメントに調整
        UINT bufferSize = (sizeof(ShockwaveParams) + 255) & ~255;

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

        // リソースを作成
        HRESULT hr = directXCommon_->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&constantBuffer_));

        assert(SUCCEEDED(hr));

        // マップ
        hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
        assert(SUCCEEDED(hr));

        // 初期データをコピー
        *mappedData_ = params_;
    }
}
