#include "pch.h"
#include "ToneMapping.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
#include <cassert>
#include <cmath>


namespace CoreEngine
{
    void ToneMapping::OnCreateConstantBuffers()
    {
        UINT size = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), size);
        [[maybe_unused]] HRESULT hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));

        CreateAutoExposureResources();
    }

    void ToneMapping::CreateAutoExposureResources()
    {
        // 失敗しても自動露出が使えないだけでトーンマッピング自体は動作させる
        autoExposureReady_ = false;

        // ===== 輝度計測 CS のコンパイルとパイプライン構築 =====
        ShaderCompiler compiler;
        compiler.Initialize();
        reductionShaderBlob_ = compiler.CompileShader(L"LuminanceReduction.CS.hlsl", L"cs_6_0");
        if (!reductionShaderBlob_) {
            Logger::GetInstance().Errorf(LogCategory::Shader,
                "ToneMapping: LuminanceReduction.CS.hlsl のコンパイルに失敗（自動露出は無効）");
            return;
        }

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(compiler.GetDxcUtils());
        reductionReflection_ = reflectionBuilder.BuildFromComputeShader(
            reductionShaderBlob_.Get(), "ToneMappingLuminanceReduction");
        if (!reductionReflection_) {
            return;
        }

        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);
        // 1要素のバッファなのでディスクリプタを介さず Root UAV で直接バインドする
        config.ConfigureResource("gAvgLuminance", BindingStrategy::RootDescriptor);

        reductionRootSignature_ = std::make_unique<RootSignatureManager>();
        auto buildResult = reductionRootSignature_->Build(
            directXCommon_->GetDevice(), *reductionReflection_, config);
        if (!buildResult.success) {
            Logger::GetInstance().Errorf(LogCategory::Shader,
                "ToneMapping: 輝度計測 RootSignature の構築に失敗: {}", buildResult.errorMessage);
            return;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = reductionRootSignature_->GetRootSignature();
        psoDesc.CS = { reductionShaderBlob_->GetBufferPointer(), reductionShaderBlob_->GetBufferSize() };
        if (FAILED(directXCommon_->GetDevice()->CreateComputePipelineState(
                &psoDesc, IID_PPV_ARGS(&reductionPso_)))) {
            return;
        }

        // ===== 平均対数輝度の出力バッファ（DEFAULT ヒープ・UAV） =====
        {
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = sizeof(float) * 4; // 実使用は先頭4バイトのみ
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            if (FAILED(directXCommon_->GetDevice()->CreateCommittedResource(
                    &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                    IID_PPV_ARGS(&avgLogLumBuffer_)))) {
                return;
            }
        }

        // ===== リードバックリング（GPU が最大2フレーム遅延しても読み書きが重ならない） =====
        for (uint32_t i = 0; i < kReadbackCount; ++i) {
            readbackBuffers_[i] = ResourceFactory::CreateBufferResource(
                directXCommon_->GetDevice(), 256, D3D12_HEAP_TYPE_READBACK);
            if (!readbackBuffers_[i]) {
                return;
            }
            void* mapped = nullptr;
            if (FAILED(readbackBuffers_[i]->Map(0, nullptr, &mapped))) {
                return;
            }
            mappedReadback_[i] = static_cast<const float*>(mapped);
        }

        autoExposureReady_ = true;
    }

    void ToneMapping::UpdateAutoExposureAdaptation()
    {
        // 2フレーム前に計測を書き込んだスロット（GPU 完了済み）を読む
        if (reductionFrameCounter_ < kReadbackCount - 1) {
            return; // まだ有効な計測値が無い
        }
        const uint32_t slot = static_cast<uint32_t>((reductionFrameCounter_ + 1) % kReadbackCount);
        if (!mappedReadback_[slot]) {
            return;
        }

        // 計測値は線形輝度の算術平均（カメラの平均測光相当）
        const float targetLuminance = *mappedReadback_[slot];

        if (!adaptationInitialized_) {
            // 初回は目標値へ即座に合わせる（起動直後に真っ白/真っ黒から順応し始めるのを防ぐ）
            adaptedLuminance_ = targetLuminance;
            adaptationInitialized_ = true;
        } else {
            // 目の明暗順応: 目標輝度へ指数的に追従する
            const float blend = 1.0f - std::exp(-deltaTime_ * adaptationSpeed_);
            adaptedLuminance_ += (targetLuminance - adaptedLuminance_) * blend;
        }

        // ===== ターゲットキーの決定 =====
        // 固定キー（0.18）への正規化は「どんな暗さのシーンも中間グレーへ持ち上げる」ため、
        // 薄暮の反太陽側の空が昼のような明るさに見えてしまう（暗さの絶対感が消える）。
        // 人間の目は完全には順応しないので、Krawczyk 2005 の自動キー
        // （シーンが暗いほど小さいキー = 暗い出力へ写す）で明暗の絶対感を保つ。
        // 例: 昼(順応輝度~1.0)でキー~0.17（従来とほぼ同じ）、薄暮(~0.1)で~0.06、夜(~0.01)で~0.03
        float targetKey = keyValue_;
        if (preserveSceneBrightness_) {
            const float krawczykKey =
                1.03f - 2.0f / (2.0f + std::log10(adaptedLuminance_ + 1.0f));
            // ユーザー設定のキー値は 0.18 を基準とした相対倍率として効かせる
            targetKey = krawczykKey * (keyValue_ / 0.18f);
        }
        currentKey_ = targetKey;

        // 順応輝度が targetKey へ写る露出を求める
        autoEV_ = std::clamp(
            std::log2(targetKey / std::max(adaptedLuminance_, 1e-6f)),
            minAutoEV_, maxAutoEV_);
    }

    void ToneMapping::RecordLuminanceReduction(
        ID3D12GraphicsCommandList* cmdList, D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
    {
        cmdList->SetComputeRootSignature(reductionRootSignature_->GetRootSignature());
        cmdList->SetPipelineState(reductionPso_.Get());

        const int texIdx = reductionReflection_->GetRootParameterIndexByName("gTexture");
        const int cbIdx = reductionReflection_->GetRootParameterIndexByName("ScreenParams");
        const int uavIdx = reductionReflection_->GetRootParameterIndexByName("gAvgLuminance");
        if (texIdx >= 0) cmdList->SetComputeRootDescriptorTable(texIdx, inputSrvHandle);
        if (cbIdx >= 0) cmdList->SetComputeRootConstantBufferView(cbIdx, screenParamsCB_->GetGPUVirtualAddress());
        if (uavIdx >= 0) cmdList->SetComputeRootUnorderedAccessView(uavIdx, avgLogLumBuffer_->GetGPUVirtualAddress());

        // 1グループのみ（シェーダー側が 64x64 グリッドを分担して groupshared で縮約する）
        cmdList->Dispatch(1, 1, 1);

        // 書き込み完了を待ってリードバックへコピー
        D3D12_RESOURCE_BARRIER uavBarrier{};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = avgLogLumBuffer_.Get();
        cmdList->ResourceBarrier(1, &uavBarrier);

        D3D12_RESOURCE_BARRIER toCopySource{};
        toCopySource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopySource.Transition.pResource = avgLogLumBuffer_.Get();
        toCopySource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toCopySource.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toCopySource.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        cmdList->ResourceBarrier(1, &toCopySource);

        const uint32_t writeSlot = static_cast<uint32_t>(reductionFrameCounter_ % kReadbackCount);
        cmdList->CopyBufferRegion(
            readbackBuffers_[writeSlot].Get(), 0, avgLogLumBuffer_.Get(), 0, sizeof(float));

        D3D12_RESOURCE_BARRIER backToUav = toCopySource;
        backToUav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        backToUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        cmdList->ResourceBarrier(1, &backToUav);

        ++reductionFrameCounter_;
    }

    void ToneMapping::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth = width;
            mappedScreenParams_->screenHeight = height;
            // 自動露出有効時: 自動EV + 手動EV（補正オフセット）。無効時: 手動EVのみ（従来動作）
            const bool useAuto = autoExposureEnabled_ && autoExposureReady_;
            mappedScreenParams_->exposureEV = exposureEV_ + (useAuto ? autoEV_ : 0.0f);
        }
    }

    void ToneMapping::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        const bool useAutoExposure = autoExposureEnabled_ && autoExposureReady_;

        // 過去フレームの計測値で順応を進めてから、今フレームの露出を CB へ書き込む
        if (useAutoExposure) {
            UpdateAutoExposureAdaptation();
        }
        UpdateScreenConstantBuffer(width, height);

        auto* cmdList = directXCommon_->GetCommandList();

        // 今フレームの入力輝度を計測する（結果は2フレーム後の順応更新で使われる）
        if (useAutoExposure) {
            RecordLuminanceReduction(cmdList, inputSrvHandle);
        }

        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx = GetRootParamIndex("gTexture");
        int outputIdx = GetRootParamIndex("gOutput");
        int screenParamsIdx = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0)      cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)       cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (screenParamsIdx >= 0) cmdList->SetComputeRootConstantBufferView(screenParamsIdx, screenParamsCB_->GetGPUVirtualAddress());

        uint32_t groupX = (width + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void ToneMapping::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("ToneMapping");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::Text("ACES トーンマッピング（HDR→LDR変換）");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "このエフェクトは常に有効です。");

        // 自動露出: 画面の平均輝度から露出を毎フレーム計算し、目の明暗順応のように時間追従する。
        // 太陽の位置を動かすと露出も計算で追従するため、時刻ごとの手動調整が不要になる
        ImGui::Checkbox("自動露出（Auto Exposure）", &autoExposureEnabled_);
        if (autoExposureEnabled_) {
            if (!autoExposureReady_) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                    "輝度計測リソースの初期化に失敗しています（ログ参照）");
            } else {
                ImGui::Text("自動EV: %.2f（合計EV: %.2f）", autoEV_, autoEV_ + exposureEV_);
                ImGui::Text("順応輝度: %.4f / ターゲットキー: %.3f", adaptedLuminance_, currentKey_);
                // OFF だと全シーンが中間グレーへ正規化され、薄暮の空が昼のように明るくなる
                ImGui::Checkbox("明暗の絶対感を保持（暗いシーンは暗く）", &preserveSceneBrightness_);
                ImGui::SliderFloat("順応速度 [1/s]", &adaptationSpeed_, 0.1f, 10.0f, "%.1f");
                ImGui::SliderFloat("キー値（明るさの基準）", &keyValue_, 0.05f, 0.5f, "%.2f");
                ImGui::SliderFloat("自動EV下限", &minAutoEV_, -8.0f, 0.0f, "%.1f");
                ImGui::SliderFloat("自動EV上限", &maxAutoEV_, 0.0f, 8.0f, "%.1f");
            }
        }

        // 露出補正: ACES 適用前に exp2(EV) を乗算する。
        // 自動露出が有効な場合は自動EVへの加算オフセットとして働く
        ImGui::SliderFloat(autoExposureEnabled_ ? "露出補正 [EV]（オフセット）" : "露出補正 [EV]",
                           &exposureEV_, -6.0f, 6.0f, "%.2f");
        if (ImGui::Button("露出をリセット")) {
            exposureEV_ = 0.0f;
        }
        UI::Separator();
        ImGui::PopID();
#endif // USE_IMGUI
    }
}
