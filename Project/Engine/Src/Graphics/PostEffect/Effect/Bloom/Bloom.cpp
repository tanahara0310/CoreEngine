#include "pch.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Bloom.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/Resource/UavTexture.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h"
#include "Utility/CVar/CVar.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
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

        CVar<float> cvDirtIntensity{
            "r.Bloom.DirtIntensity", 0.0f,
            "レンズダート（レンズの汚れがブルーム光で光る）の強さ。0 で無効。"
            "逆光や太陽グリッターの強いカットで実写的なディテールになる",
            CVarRange{ 0.0f, 4.0f } };

        CVar<bool> cvEnabled{
            "r.Bloom.Enabled", false,
            "ブルームを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.Bloom";
    }

    void Bloom::OnCreateResources()
    {
        internalPipelinesReady_ = BuildInternalPipelines({
            { downsamplePipeline_, L"BloomDownsample.CS.hlsl", "Downsample" },
            { upsamplePipeline_,   L"BloomUpsample.CS.hlsl",   "Upsample" },
            { dirtGenPipeline_,    L"BloomDirtGen.CS.hlsl",    "DirtGen" },
        });
        if (!internalPipelinesReady_) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "Bloom: ダウン/アップサンプルのパイプライン構築に失敗（ブルームは無効）");
        }

        // ダートは失敗してもブルーム本体は動かす（DirtIntensity=0 相当になるだけ）
        dirtResourcesReady_ = internalPipelinesReady_ && CreateDirtResources();
        if (!dirtResourcesReady_) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "Bloom: ダートマスクリソースの構築に失敗（レンズダートは無効）");
        }
    }

    bool Bloom::CreateDirtResources()
    {
        // レンズダート（汚れ）テクスチャ。未指定でも既定の手続き的パターンを焼くので必ず作る
        auto* device = graphicsCore_->GetDevice();
        DescriptorAllocator* descriptorAllocator = graphicsCore_->GetDescriptorAllocator();
        if (!descriptorAllocator) {
            return false;
        }

        return UavTexture::Create(device, *descriptorAllocator, dirtTexture_,
            UavTexture::MakeDesc(kDirtTextureSize, kDirtTextureSize, 1, DXGI_FORMAT_R16_FLOAT),
            dirtSrvHandle_, "BloomDirt_SRV", dirtUavHandle_, "BloomDirt_UAV");
    }

    void Bloom::RecordDirtGenerationIfNeeded(ID3D12GraphicsCommandList* cmdList)
    {
        if (dirtGenerated_ || !dirtResourcesReady_) {
            return;
        }

        DirtGenParams params{};
        params.textureSize = kDirtTextureSize;

        DispatchPass(cmdList, dirtGenPipeline_,
            { { "gOutput", dirtUavHandle_.gpuHandle } },
            { { "DirtGenParams", UploadConstants(params) } },
            kDirtTextureSize, kDirtTextureSize);

        {
            BarrierBatch batch(cmdList);
            batch.UAV(dirtTexture_);
            batch.Transition(dirtTexture_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        dirtGenerated_ = true;
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
        if (context.reads.empty()) {
            return;
        }
        const uint32_t sourceWidth  = (mipIndex == 0) ? baseWidth_  : mipWidth_[mipIndex - 1];
        const uint32_t sourceHeight = (mipIndex == 0) ? baseHeight_ : mipHeight_[mipIndex - 1];

        DownsampleParams params{};
        params.outputSize[0]  = context.width;
        params.outputSize[1]  = context.height;
        params.sourceSize[0]  = sourceWidth;
        params.sourceSize[1]  = sourceHeight;
        params.threshold      = cvThreshold.Get();
        params.softKnee       = cvSoftKnee.Get();
        // 閾値は最初の 1 回だけ。各段で掛けるとぼかすほど二重に削られる
        params.applyPrefilter = (mipIndex == 0) ? 1u : 0u;

        DispatchPass(context.cmdList, downsamplePipeline_,
            { { "gSource", context.reads[0] }, { "gOutput", context.output } },
            { { "BloomDownsampleParams", UploadConstants(params) } },
            context.width, context.height);
    }

    void Bloom::RecordUpsample(const PostEffectPassContext& context, int upIndex)
    {
        if (context.reads.size() < 2) {
            return;
        }
        UpsampleParams params{};
        params.outputSize[0] = context.width;
        params.outputSize[1] = context.height;
        params.lowerSize[0]  = mipWidth_[upIndex + 1];
        params.lowerSize[1]  = mipHeight_[upIndex + 1];

        DispatchPass(context.cmdList, upsamplePipeline_,
            { { "gLower", context.reads[0] }, { "gSame", context.reads[1] }, { "gOutput", context.output } },
            { { "BloomUpsampleParams", UploadConstants(params) } },
            context.width, context.height);
    }

    void Bloom::RecordComposite(const PostEffectPassContext& context)
    {
        if (context.reads.size() < 2) {
            return;
        }
        CompositeParams params{};
        params.outputSize[0] = context.width;
        params.outputSize[1] = context.height;
        params.bloomSize[0]  = mipWidth_[0];
        params.bloomSize[1]  = mipHeight_[0];
        params.intensity     = cvIntensity.Get();
        params.dirtIntensity = dirtResourcesReady_ ? cvDirtIntensity.Get() : 0.0f;
        params.dirtSize      = kDirtTextureSize;

        auto* cmdList = context.cmdList;

        // ダートマスクは初回だけ手続き生成する（エフェクト私有リソースなのでグラフ外・手動バリア）
        RecordDirtGenerationIfNeeded(cmdList);

        // 合成は基底が構築した PSO（GetComputeShaderPath が返す BloomComposite.CS.hlsl）を使う。
        // ダートは作れたときだけ差す
        DispatchPass(cmdList,
            { { "gScene", context.reads[0] }, { "gBloom", context.reads[1] },
              { dirtResourcesReady_ ? "gDirt" : nullptr, dirtSrvHandle_.gpuHandle },
              { "gOutput", context.output } },
            { { "BloomCompositeParams", UploadConstants(params) } },
            context.width, context.height);
    }

    void Bloom::DrawImGui()
    {
#ifdef CORE_EDITOR
        DrawCVarEffectImGui("BloomParams", kCVarPrefix);
#endif // CORE_EDITOR
    }

    CVar<bool>* Bloom::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
