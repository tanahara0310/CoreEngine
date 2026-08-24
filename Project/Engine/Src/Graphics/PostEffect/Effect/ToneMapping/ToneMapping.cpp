#include "pch.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "ToneMapping.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Utility/Logger/Logger.h"
#include "Utility/CVar/CVar.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <algorithm>
#include <cassert>
#include <cmath>


namespace CoreEngine
{
    namespace
    {
        CVar<float> cvExposureEV{
            "r.AutoExposure.ExposureEV", 0.0f,
            "露出補正 [EV]。+1 で明るさ2倍。自動露出が有効なときは加算オフセットとして働く",
            CVarRange{ -6.0f, 6.0f } };

        CVar<bool> cvAutoExposureEnabled{
            "r.AutoExposure.Enabled", false,
            "自動露出。画面または照明の明るさから露出を毎フレーム計算し、目の明暗順応のように追従する" };

        CVar<bool> cvMeteringIllumination{
            "r.AutoExposure.MeteringIllumination", true,
            "照明駆動測光を使う。OFF だと画面平均測光になり、カメラの向きで露出がポンピングする" };

        CVar<bool> cvPreserveSceneBrightness{
            "r.AutoExposure.PreserveSceneBrightness", true,
            "明暗の絶対感を保持する（Krawczyk 自動キー）。"
            "OFF だと全シーンが中間グレーへ正規化され、薄暮の空が昼のように明るくなる" };

        CVar<float> cvAdaptationSpeedUp{
            "r.AutoExposure.SpeedUp", 8.0f,
            "シーンが明るくなったときの順応の速さ [1/s]。8.0 で 95% 到達 ≈0.4s。"
            "目は眩しさに素早く順応するので暗くなる側より速くする",
            CVarRange{ 0.1f, 20.0f } };

        CVar<float> cvAdaptationSpeedDown{
            "r.AutoExposure.SpeedDown", 3.0f,
            "シーンが暗くなったときの順応の速さ [1/s]。目の暗順応は遅いので明るい側より小さく。"
            "トンネルへ入った直後にしばらく暗いままになる効果が出る",
            CVarRange{ 0.1f, 20.0f } };

        CVar<float> cvLowPercentile{
            "r.AutoExposure.LowPercentile", 0.5f,
            "測光ヒストグラムの下側カット。この割合より暗いサンプルは露出計算から除外する。"
            "大面積の暗部（影の地面）が露出を持ち上げすぎるのを防ぐ",
            CVarRange{ 0.0f, 0.95f } };

        CVar<float> cvHighPercentile{
            "r.AutoExposure.HighPercentile", 0.9f,
            "測光ヒストグラムの上側カット。この割合より明るいサンプルは露出計算から除外する。"
            "太陽や水面グリッターが画面へ入った瞬間に全体が沈む「呼吸」を防ぐ",
            CVarRange{ 0.05f, 1.0f } };

        CVar<float> cvKeyValue{
            "r.AutoExposure.KeyValue", 0.18f,
            "順応輝度を写す明るさの基準（中間グレー）。自動キー時は相対倍率として効く",
            CVarRange{ 0.05f, 0.5f } };

        CVar<float> cvMinAutoEV{
            "r.AutoExposure.MinEV", -4.0f,
            "自動EVの下限。真っ白な画面で絞りすぎないようにする",
            CVarRange{ -12.0f, 0.0f } };

        CVar<float> cvMaxAutoEV{
            "r.AutoExposure.MaxEV", 8.0f,
            "自動EVの上限。月夜には約 +5EV 必要で、下げると夜がクランプされて暗く沈む",
            CVarRange{ 0.0f, 12.0f } };

        CVar<float> cvReferenceLuminance{
            "r.AutoExposure.ReferenceLuminance", 2.0f,
            "自動EVが 0 になる基準輝度。SceneColor は 0EV で適正になるよう較正済みのため、"
            "絶対露出ではなくこの基準からの相対補正として働かせる（既定 2.0 は晴天正午の実測値）",
            CVarRange{ 0.05f, 10.0f } };

        CVar<int> cvToneMapOperator{
            "r.ToneMapping.Operator", 0,
            "トーンカーブ。0=ACES（コントラスト強め・従来） 1=GT（中間調が素直・グランツーリスモ方式） "
            "2=AgX（高彩度の光源が色相を回さず白へ抜ける・Blender 4.x 既定）。"
            "切り替えたら r.AutoExposure.ReferenceLuminance の再較正を推奨",
            CVarRange{ 0.0f, 2.0f } };

        constexpr const char* kCVarPrefix = "r.AutoExposure";
        constexpr const char* kToneMapCVarPrefix = "r.ToneMapping";
    }

    void ToneMapping::SetAutoExposureEnabled(bool enabled)
    {
        cvAutoExposureEnabled.Set(enabled);
    }

    bool ToneMapping::IsAutoExposureEnabled() const
    {
        return cvAutoExposureEnabled.Get();
    }

    float ToneMapping::GetAutoExposureEV() const
    {
        return cvAutoExposureEnabled.Get() ? autoEV_ : 0.0f;
    }

    void ToneMapping::CalibrateReferenceToCurrent()
    {
        cvReferenceLuminance.Set(std::max(adaptedLuminance_, 1e-6f));
    }

    void ToneMapping::OnCreateConstantBuffers()
    {
        UINT size = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(graphicsCore_->GetDevice(), size);
        [[maybe_unused]] HRESULT hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));

        CreateAutoExposureResources();
    }

    void ToneMapping::CreateAutoExposureResources()
    {
        // 失敗しても自動露出が使えないだけでトーンマッピング自体は動作させる
        autoExposureReady_ = false;

        // ===== 輝度計測 CS のコンパイルとパイプライン構築 =====
        // コンパイルとリフレクションはエンジン共有のキャッシュが担当する
        const ShaderProgram* reductionProgram = shaderProgramCache_->GetOrCreateCompute(
            L"LuminanceReduction.CS.hlsl", "ToneMappingLuminanceReduction");
        if (!reductionProgram) {
            Logger::GetInstance().Errorf(LogCategory::Shader,
                "ToneMapping: LuminanceReduction.CS.hlsl のコンパイルに失敗（自動露出は無効）");
            return;
        }
        reductionShaderBlob_ = reductionProgram->GetCS();
        reductionReflection_ = &reductionProgram->GetReflection();

        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);
        // 1要素のバッファなのでディスクリプタを介さず Root UAV で直接バインドする
        config.ConfigureResource("gAvgLuminance", BindingStrategy::RootDescriptor);

        reductionRootSignature_ = std::make_unique<RootSignatureManager>();
        auto buildResult = reductionRootSignature_->Build(
            graphicsCore_->GetDevice(), *reductionReflection_, config);
        if (!buildResult.success) {
            Logger::GetInstance().Errorf(LogCategory::Shader,
                "ToneMapping: 輝度計測 RootSignature の構築に失敗: {}", buildResult.errorMessage);
            return;
        }

        reductionPso_ = ComputePipelineUtil::Create(
            graphicsCore_->GetDevice(), reductionRootSignature_->GetRootSignature(),
            reductionShaderBlob_, "ToneMapping_LuminanceReduction");
        if (!reductionPso_) {
            return;
        }

        // ===== ヒストグラム測光の設定バッファ（b1・永続マップ） =====
        {
            const UINT size = (sizeof(HistogramMeteringParams) + 255) & ~255u;
            histogramParamsCB_ = ResourceFactory::CreateBufferResource(graphicsCore_->GetDevice(), size);
            if (!histogramParamsCB_ ||
                FAILED(histogramParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedHistogramParams_)))) {
                return;
            }
        }

        // ===== 測光結果の出力バッファ（DEFAULT ヒープ・UAV） =====
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
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
            if (FAILED(graphicsCore_->GetDevice()->CreateCommittedResource(
                    &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                    IID_PPV_ARGS(&buffer)))) {
                return;
            }
            avgLogLumBuffer_.Reset(std::move(buffer), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        // ===== リードバックリング（GPU が最大2フレーム遅延しても読み書きが重ならない） =====
        for (uint32_t i = 0; i < kReadbackCount; ++i) {
            readbackBuffers_[i] = ResourceFactory::CreateBufferResource(
                graphicsCore_->GetDevice(), 256, D3D12_HEAP_TYPE_READBACK);
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
        float targetLuminance = 0.0f;
        if (cvMeteringIllumination.Get() && illuminationValid_) {
            // 照明駆動測光: 大気システムが解析した「シーンの照明状態の代表輝度」を使う。
            // カメラの向き（画面の構図）に依存しないため、地面/空を見ても露出が変わらない
            targetLuminance = illuminationLuminance_;
        } else {
            // 画面平均測光: 2フレーム前に計測を書き込んだスロット（GPU 完了済み）を読む
            if (reductionFrameCounter_ < kReadbackCount - 1) {
                return; // まだ有効な計測値が無い
            }
            const uint32_t slot = static_cast<uint32_t>((reductionFrameCounter_ + 1) % kReadbackCount);
            if (!mappedReadback_[slot]) {
                return;
            }
            // 計測値は線形輝度の算術平均（カメラの平均測光相当）
            targetLuminance = *mappedReadback_[slot];
        }

        if (!adaptationInitialized_) {
            // 初回は目標値へ即座に合わせる（起動直後に真っ白/真っ黒から順応し始めるのを防ぐ）
            adaptedLuminance_ = targetLuminance;
            adaptationInitialized_ = true;
        } else {
            // 目の明暗順応: 目標輝度へ指数的に追従する。
            // 明るくなる側は速く（眩しさへの防御）、暗くなる側は遅く（暗順応）
            const float speed = (targetLuminance > adaptedLuminance_)
                ? cvAdaptationSpeedUp.Get()
                : cvAdaptationSpeedDown.Get();
            const float blend = 1.0f - std::exp(-deltaTime_ * speed);
            adaptedLuminance_ += (targetLuminance - adaptedLuminance_) * blend;
        }

        // ===== ターゲットキーの決定 =====
        // 固定キー（0.18）だと暗いシーンまで中間グレーへ持ち上がり、暗さの絶対感が消える。
        // Krawczyk 2005 の自動キー（暗いシーンほど小さいキー）で明暗の絶対感を保つ。
        const float targetKey = KeyForLuminance(adaptedLuminance_);
        currentKey_ = targetKey;

        // ===== 自動 EV の決定 =====
        // SceneColor は 0EV でそのまま適正になるよう較正済みなので、写真的な絶対露出を重ねると
        // 二重に絞られる（晴天昼で約 -3.2EV まで落ちる）。
        // そこで基準輝度での EV を 0 点として差し引き、相対補正にする。
        const float referenceLuminance = cvReferenceLuminance.Get();
        const float rawEV = std::log2(targetKey / std::max(adaptedLuminance_, 1e-6f));
        const float referenceEV = std::log2(
            KeyForLuminance(referenceLuminance) / std::max(referenceLuminance, 1e-6f));

        autoEV_ = std::clamp(rawEV - referenceEV, cvMinAutoEV.Get(), cvMaxAutoEV.Get());
    }

    float ToneMapping::KeyForLuminance(float luminance) const
    {
        if (!cvPreserveSceneBrightness.Get()) {
            return cvKeyValue.Get();
        }
        // Krawczyk 2005 の自動キー（シーンが暗いほど小さいキー = 暗い出力へ写す）。
        // ユーザー設定のキー値は 0.18 を基準とした相対倍率として効かせる
        const float krawczykKey = 1.03f - 2.0f / (2.0f + std::log10(luminance + 1.0f));
        return krawczykKey * (cvKeyValue.Get() / 0.18f);
    }

    void ToneMapping::RecordLuminanceReduction(
        ID3D12GraphicsCommandList* cmdList, D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
    {
        // 百分位カットの設定を書き込む（low >= high の設定ミスは境界を離して救済する）
        if (mappedHistogramParams_) {
            const float high = cvHighPercentile.Get();
            mappedHistogramParams_->lowPercentile = std::min(cvLowPercentile.Get(), high - 0.01f);
            mappedHistogramParams_->highPercentile = high;
        }

        cmdList->SetComputeRootSignature(reductionRootSignature_->GetRootSignature());
        cmdList->SetPipelineState(reductionPso_.Get());

        const int texIdx = reductionReflection_->GetRootParameterIndexByName("gTexture");
        const int cbIdx = reductionReflection_->GetRootParameterIndexByName("ScreenParams");
        const int histIdx = reductionReflection_->GetRootParameterIndexByName("HistogramParams");
        const int uavIdx = reductionReflection_->GetRootParameterIndexByName("gAvgLuminance");
        if (texIdx >= 0) cmdList->SetComputeRootDescriptorTable(texIdx, inputSrvHandle);
        if (cbIdx >= 0) cmdList->SetComputeRootConstantBufferView(cbIdx, screenParamsCB_->GetGPUVirtualAddress());
        if (histIdx >= 0) cmdList->SetComputeRootConstantBufferView(histIdx, histogramParamsCB_->GetGPUVirtualAddress());
        if (uavIdx >= 0) cmdList->SetComputeRootUnorderedAccessView(uavIdx, avgLogLumBuffer_.GpuAddress());

        // 1グループのみ（シェーダー側が 64x64 グリッドを分担して groupshared で縮約する）
        cmdList->Dispatch(1, 1, 1);

        // 書き込み完了を待ってリードバックへコピー
        {
            BarrierBatch batch(cmdList);
            batch.UAV(avgLogLumBuffer_);
            batch.Transition(avgLogLumBuffer_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        }

        const uint32_t writeSlot = static_cast<uint32_t>(reductionFrameCounter_ % kReadbackCount);
        cmdList->CopyBufferRegion(
            readbackBuffers_[writeSlot].Get(), 0, avgLogLumBuffer_.Get(), 0, sizeof(float));

        Barrier::Transition(cmdList, avgLogLumBuffer_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        ++reductionFrameCounter_;
    }

    void ToneMapping::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth = width;
            mappedScreenParams_->screenHeight = height;
            // 自動露出有効時: 自動EV + 手動EV（補正オフセット）。無効時: 手動EVのみ（従来動作）
            const bool useAuto = cvAutoExposureEnabled.Get() && autoExposureReady_;
            mappedScreenParams_->exposureEV = cvExposureEV.Get() + (useAuto ? autoEV_ : 0.0f);
            mappedScreenParams_->toneMapOperator =
                static_cast<uint32_t>(std::clamp(cvToneMapOperator.Get(), 0, 2));
        }
    }

    void ToneMapping::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        const bool useAutoExposure = cvAutoExposureEnabled.Get() && autoExposureReady_;
        const bool useIlluminationMetering = cvMeteringIllumination.Get() && illuminationValid_;

        // 過去フレームの計測値で順応を進めてから、今フレームの露出を CB へ書き込む
        if (useAutoExposure) {
            UpdateAutoExposureAdaptation();
        }
        UpdateScreenConstantBuffer(width, height);

        auto* cmdList = graphicsCore_->GetCommandList();

        // 今フレームの入力輝度を計測する（結果は2フレーム後の順応更新で使われる）。
        // 照明駆動測光が有効な間は GPU 計測が不要なのでスキップする
        if (useAutoExposure && !useIlluminationMetering) {
            RecordLuminanceReduction(cmdList, inputSrvHandle);
        }

        // 照明輝度は毎フレーム供給される前提の消費型フラグ。
        // 供給が止まったフレーム（大気の無いシーン等）は画面平均へフォールバックする
        illuminationActiveLastFrame_ = useIlluminationMetering;
        illuminationValid_ = false;

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
        static const char* kOperatorNames[] = { "ACES", "GT", "AgX" };
        const int op = std::clamp(cvToneMapOperator.Get(), 0, 2);
        ImGui::Text("%s トーンマッピング（HDR→LDR変換）", kOperatorNames[op]);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "このエフェクトは常に有効です。");

        // 自動露出が有効なときだけ意味を持つ診断情報（計測結果なので CVar ではない）
        if (cvAutoExposureEnabled.Get()) {
            if (!autoExposureReady_) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                    "輝度計測リソースの初期化に失敗しています（ログ参照）");
            } else {
                if (cvMeteringIllumination.Get()) {
                    ImGui::Text("照明輝度: %.5f %s", illuminationLuminance_,
                        illuminationActiveLastFrame_ ? "" : "（未供給: 画面平均へフォールバック中）");
                }
                ImGui::Text("自動EV: %.2f（合計EV: %.2f）", autoEV_, autoEV_ + cvExposureEV.Get());
                ImGui::Text("順応輝度: %.4f / ターゲットキー: %.3f", adaptedLuminance_, currentKey_);

                if (ImGui::Button("今の明るさを基準にする")) {
                    CalibrateReferenceToCurrent();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "自動露出を切った状態の見た目に合う構図・時刻で押すと、\n"
                        "そこを 0EV の基準にできる");
                }
            }
            UI::Separator();
        }

        CVarUI::DrawTree(kToneMapCVarPrefix);
        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kToneMapCVarPrefix);
            CVarUI::ResetTree(kCVarPrefix);
        }
        UI::Separator();
        ImGui::PopID();
#endif // USE_IMGUI
    }
}
