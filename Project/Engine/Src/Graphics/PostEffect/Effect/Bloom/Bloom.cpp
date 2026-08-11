#include "pch.h"
#include "Bloom.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h"
#include "Utility/CVar/CVar.h"
#include "Utility/Logger/Logger.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <algorithm>
#include <cassert>
#include <string>


namespace CoreEngine
{
    namespace
    {
        CVar<float> cvThreshold{
            "r.Bloom.Threshold", 0.8f,
            "ブルームを発生させる輝度の閾値。低いほど広い範囲が光る",
            CVarRange{ 0.0f, 2.0f } };

        CVar<float> cvIntensity{
            "r.Bloom.Intensity", 1.0f,
            "ブルームの強度",
            CVarRange{ 0.0f, 3.0f } };

        // 旧 "r.Bloom.BlurRadius" はミップチェーン化で意味が無くなったため廃止。
        // 広がりはミップ段数（Bloom::kMipCount）が決めるので、半径のつまみは存在しない。

        CVar<float> cvSoftKnee{
            "r.Bloom.SoftKnee", 0.5f,
            "閾値付近のなめらかさ。0 で硬い切り替わり",
            CVarRange{ 0.0f, 1.0f } };

        CVar<bool> cvEnabled{
            "r.Bloom.Enabled", false,
            "ブルームを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.Bloom";
    }

    namespace {
        /// @brief 8x8 スレッドグループでの必要グループ数
        constexpr uint32_t DispatchCount(uint32_t size) { return (size + 7) / 8; }

        /// @brief 定数バッファを 1 本作って永続マップする
        template <typename T>
        void CreateMappedCB(ID3D12Device* device, Microsoft::WRL::ComPtr<ID3D12Resource>& buffer, T*& mapped)
        {
            const UINT size = (sizeof(T) + 255) & ~255u;
            buffer = ResourceFactory::CreateBufferResource(device, size);
            [[maybe_unused]] HRESULT hr = buffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
            assert(SUCCEEDED(hr));
        }
    }

    void Bloom::OnCreateConstantBuffers()
    {
        auto* device = directXCommon_->GetDevice();

        // パスごとに解像度と役割が違うので、定数バッファもパスの数だけ要る。
        // 1 本を使い回すと、GPU が読むのは記録より後なので最後のパスの値で全段が実行される
        for (int i = 0; i < kMipCount; ++i) {
            CreateMappedCB(device, downParamsCB_[i], mappedDownParams_[i]);
        }
        for (int i = 0; i < kMipCount - 1; ++i) {
            CreateMappedCB(device, upParamsCB_[i], mappedUpParams_[i]);
        }
        CreateMappedCB(device, compositeParamsCB_, mappedCompositeParams_);

        internalPipelinesReady_ = CreateInternalPipelines();
        if (!internalPipelinesReady_) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "Bloom: ダウン/アップサンプルのパイプライン構築に失敗（ブルームは無効）");
        }
    }

    bool Bloom::CreateInternalPipelines()
    {
        auto* device = directXCommon_->GetDevice();

        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        struct Entry {
            CustomShaderPipeline& pipeline;
            const ICustomShaderProvider& provider;
            const char* name;
        };
        Entry entries[] = {
            { downsamplePipeline_, downsampleProvider_, "Downsample" },
            { upsamplePipeline_,   upsampleProvider_,   "Upsample" },
        };

        for (Entry& entry : entries) {
            const bool built = entry.pipeline.Build(device, shaderCompiler, reflectionBuilder, entry.provider);
            if (!built || !entry.pipeline.HasComputePSO()) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "Bloom: {} パイプラインの構築に失敗", entry.name);
                return false;
            }
        }
        return true;
    }

    void Bloom::BuildPasses(PostEffectGraphBuilder& builder)
    {
        // パイプラインが無いフレームは何もせず素通し（積まなければ前段の出力がそのまま次段へ行く）
        if (!internalPipelinesReady_) {
            return;
        }

        baseWidth_  = builder.BaseWidth();
        baseHeight_ = builder.BaseHeight();
        if (baseWidth_ == 0 || baseHeight_ == 0) {
            return;
        }

        // 各段の解像度を先に確定させる。record 側はここで決めた値を参照するだけにする
        for (int i = 0; i < kMipCount; ++i) {
            const uint32_t divisor = 1u << (i + 1);
            mipWidth_[i]  = std::max(1u, baseWidth_  / divisor);
            mipHeight_[i] = std::max(1u, baseHeight_ / divisor);
        }

        std::vector<PostEffectResourceRef> downMips(kMipCount);
        for (int i = 0; i < kMipCount; ++i) {
            downMips[i] = builder.CreateTransient(1.0f / static_cast<float>(1u << (i + 1)));
            if (!downMips[i].IsValid()) {
                return;
            }
        }

        // 上りは下りとは別のリソースへ書く。同じリソースを SRV と UAV に同時に割り当てられないため
        std::vector<PostEffectResourceRef> upMips(kMipCount - 1);
        for (int i = 0; i < kMipCount - 1; ++i) {
            upMips[i] = builder.CreateTransient(1.0f / static_cast<float>(1u << (i + 1)));
            if (!upMips[i].IsValid()) {
                return;
            }
        }

        // ---- ダウンサンプル: Input → 1/2 → 1/4 → ... → 1/64 ----
        for (int i = 0; i < kMipCount; ++i) {
            const PostEffectResourceRef source = (i == 0) ? builder.Input() : downMips[i - 1];
            builder.AddComputePass(
                "Bloom.Down" + std::to_string(i), { source }, downMips[i],
                [this, i](const PostEffectPassContext& passContext) { RecordDownsample(passContext, i); });
        }

        // ---- アップサンプル: 1/64 を 1/32 へ足し込み…最後は 1/2 ----
        for (int i = kMipCount - 2; i >= 0; --i) {
            const PostEffectResourceRef lower = (i == kMipCount - 2) ? downMips[kMipCount - 1] : upMips[i + 1];
            builder.AddComputePass(
                "Bloom.Up" + std::to_string(i), { lower, downMips[i] }, upMips[i],
                [this, i](const PostEffectPassContext& passContext) { RecordUpsample(passContext, i); });
        }

        // ---- 合成: シーン + ブルーム*強度 ----
        builder.AddComputePass(
            "Bloom.Composite", { builder.Input(), upMips[0] }, builder.ChainOutput(),
            [this](const PostEffectPassContext& passContext) { RecordComposite(passContext); });
    }

    void Bloom::RecordDownsample(const PostEffectPassContext& context, int mipIndex)
    {
        DownsampleParams* params = mappedDownParams_[mipIndex];
        if (!params || context.reads.empty()) {
            return;
        }
        const uint32_t sourceWidth  = (mipIndex == 0) ? baseWidth_  : mipWidth_[mipIndex - 1];
        const uint32_t sourceHeight = (mipIndex == 0) ? baseHeight_ : mipHeight_[mipIndex - 1];

        params->outputSize[0]  = context.width;
        params->outputSize[1]  = context.height;
        params->sourceSize[0]  = sourceWidth;
        params->sourceSize[1]  = sourceHeight;
        params->threshold      = cvThreshold.Get();
        params->softKnee       = cvSoftKnee.Get();
        // 閾値は最初の 1 回だけ。各段で掛けるとぼかすほど二重に削られる
        params->applyPrefilter = (mipIndex == 0) ? 1u : 0u;

        auto* cmdList = context.cmdList;
        cmdList->SetComputeRootSignature(downsamplePipeline_.GetComputeRootSignature());
        cmdList->SetPipelineState(downsamplePipeline_.GetComputePSO());

        const int sourceIdx = downsamplePipeline_.GetComputeRootParamIndex("gSource");
        const int outputIdx = downsamplePipeline_.GetComputeRootParamIndex("gOutput");
        const int paramsIdx = downsamplePipeline_.GetComputeRootParamIndex("BloomDownsampleParams");

        if (sourceIdx >= 0) cmdList->SetComputeRootDescriptorTable(sourceIdx, context.reads[0]);
        if (outputIdx >= 0) cmdList->SetComputeRootDescriptorTable(outputIdx, context.output);
        if (paramsIdx >= 0) cmdList->SetComputeRootConstantBufferView(paramsIdx, downParamsCB_[mipIndex]->GetGPUVirtualAddress());

        cmdList->Dispatch(DispatchCount(context.width), DispatchCount(context.height), 1);
    }

    void Bloom::RecordUpsample(const PostEffectPassContext& context, int upIndex)
    {
        UpsampleParams* params = mappedUpParams_[upIndex];
        if (!params || context.reads.size() < 2) {
            return;
        }
        params->outputSize[0] = context.width;
        params->outputSize[1] = context.height;
        params->lowerSize[0]  = mipWidth_[upIndex + 1];
        params->lowerSize[1]  = mipHeight_[upIndex + 1];

        auto* cmdList = context.cmdList;
        cmdList->SetComputeRootSignature(upsamplePipeline_.GetComputeRootSignature());
        cmdList->SetPipelineState(upsamplePipeline_.GetComputePSO());

        const int lowerIdx  = upsamplePipeline_.GetComputeRootParamIndex("gLower");
        const int sameIdx   = upsamplePipeline_.GetComputeRootParamIndex("gSame");
        const int outputIdx = upsamplePipeline_.GetComputeRootParamIndex("gOutput");
        const int paramsIdx = upsamplePipeline_.GetComputeRootParamIndex("BloomUpsampleParams");

        if (lowerIdx >= 0)  cmdList->SetComputeRootDescriptorTable(lowerIdx, context.reads[0]);
        if (sameIdx >= 0)   cmdList->SetComputeRootDescriptorTable(sameIdx, context.reads[1]);
        if (outputIdx >= 0) cmdList->SetComputeRootDescriptorTable(outputIdx, context.output);
        if (paramsIdx >= 0) cmdList->SetComputeRootConstantBufferView(paramsIdx, upParamsCB_[upIndex]->GetGPUVirtualAddress());

        cmdList->Dispatch(DispatchCount(context.width), DispatchCount(context.height), 1);
    }

    void Bloom::RecordComposite(const PostEffectPassContext& context)
    {
        if (!mappedCompositeParams_ || context.reads.size() < 2) {
            return;
        }
        mappedCompositeParams_->outputSize[0] = context.width;
        mappedCompositeParams_->outputSize[1] = context.height;
        mappedCompositeParams_->bloomSize[0]  = mipWidth_[0];
        mappedCompositeParams_->bloomSize[1]  = mipHeight_[0];
        mappedCompositeParams_->intensity     = cvIntensity.Get();

        // 合成は基底が構築した PSO（GetComputeShaderPath が返す BloomComposite.CS.hlsl）を使う
        auto* cmdList = context.cmdList;
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        const int sceneIdx  = GetRootParamIndex("gScene");
        const int bloomIdx  = GetRootParamIndex("gBloom");
        const int outputIdx = GetRootParamIndex("gOutput");
        const int paramsIdx = GetRootParamIndex("BloomCompositeParams");

        if (sceneIdx >= 0)  cmdList->SetComputeRootDescriptorTable(sceneIdx, context.reads[0]);
        if (bloomIdx >= 0)  cmdList->SetComputeRootDescriptorTable(bloomIdx, context.reads[1]);
        if (outputIdx >= 0) cmdList->SetComputeRootDescriptorTable(outputIdx, context.output);
        if (paramsIdx >= 0) cmdList->SetComputeRootConstantBufferView(paramsIdx, compositeParamsCB_->GetGPUVirtualAddress());

        cmdList->Dispatch(DispatchCount(context.width), DispatchCount(context.height), 1);
    }

    void Bloom::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("BloomParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* Bloom::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
