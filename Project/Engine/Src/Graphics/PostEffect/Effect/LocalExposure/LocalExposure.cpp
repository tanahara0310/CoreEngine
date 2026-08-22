#include "pch.h"
#include "LocalExposure.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h"
#include "Utility/CVar/CVar.h"
#include "Utility/Logger/Logger.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <algorithm>
#include <cassert>


namespace CoreEngine
{
    namespace
    {
        CVar<bool> cvEnabled{
            "r.LocalExposure.Enabled", true,
            "ローカル露出（局所トーンマッピング）を有効にする",
            CVarRange{}, CVarFlags::NoUI };

        CVar<float> cvHighlightContrast{
            "r.LocalExposure.HighlightContrast", 0.8f,
            "中間グレーより明るい低周波成分の圧縮率。1で無効、小さいほど空や明部が抑えられる。"
            "効かせすぎると HDR 写真のような眠い絵になる",
            CVarRange{ 0.3f, 1.0f } };

        CVar<float> cvShadowContrast{
            "r.LocalExposure.ShadowContrast", 0.9f,
            "中間グレーより暗い低周波成分の圧縮率。1で無効、小さいほど影が持ち上がる",
            CVarRange{ 0.3f, 1.0f } };

        CVar<float> cvDetailStrength{
            "r.LocalExposure.DetailStrength", 1.0f,
            "ディテール層（模様・質感のコントラスト）の倍率。通常は 1 のまま",
            CVarRange{ 0.5f, 2.0f } };

        CVar<float> cvMiddleGreyBias{
            "r.LocalExposure.MiddleGreyBias", 0.0f,
            "明部/暗部を分けるアンカーの EV オフセット。夜間など全体が暗いシーンで"
            "全画素が暗部扱いになるときに下げる",
            CVarRange{ -6.0f, 6.0f } };

        CVar<float> cvRangeSigma{
            "r.LocalExposure.RangeSigma", 1.0f,
            "エッジ復元（バイラテラル）の輝度許容幅 [EV]。小さいほどハロが消えるが、"
            "小さすぎるとベース層が効かなくなる",
            CVarRange{ 0.25f, 4.0f } };

        constexpr const char* kCVarPrefix = "r.LocalExposure";

        /// @brief 8x8 スレッドグループでの必要グループ数
        constexpr uint32_t DispatchCount(uint32_t size) { return (size + 7) / 8; }

        /// @brief 定数バッファを 1 本作って永続マップする
        template <typename T>
    /// @brief 定数バッファを確保して常時 Map したまま保持する
        void CreateMappedCB(ID3D12Device* device, Microsoft::WRL::ComPtr<ID3D12Resource>& buffer, T*& mapped)
        {
            const UINT size = (sizeof(T) + 255) & ~255u;
            buffer = ResourceFactory::CreateBufferResource(device, size);
            [[maybe_unused]] HRESULT hr = buffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
            assert(SUCCEEDED(hr));
        }
    }

    void LocalExposure::OnCreateConstantBuffers()
    {
        auto* device = graphicsCore_->GetDevice();

        CreateMappedCB(device, downsampleParamsCB_, mappedDownsampleParams_);
        CreateMappedCB(device, blurHParamsCB_, mappedBlurHParams_);
        CreateMappedCB(device, blurVParamsCB_, mappedBlurVParams_);
        CreateMappedCB(device, applyParamsCB_, mappedApplyParams_);

        internalPipelinesReady_ = CreateInternalPipelines();
        if (!internalPipelinesReady_) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "LocalExposure: Downsample/Blur パイプラインの構築に失敗（ローカル露出は無効）");
        }
    }

    // 4 パスぶんの Compute PSO をまとめて作る。1 本でも欠けたら無効化して素通しにする
    bool LocalExposure::CreateInternalPipelines()
    {
        auto* device = graphicsCore_->GetDevice();

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
            { blurPipeline_,       blurProvider_,       "Blur" },
        };

        for (Entry& entry : entries) {
            const bool built = entry.pipeline.Build(device, shaderCompiler, reflectionBuilder, entry.provider);
            if (!built || !entry.pipeline.HasComputePSO()) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "LocalExposure: {} パイプラインの構築に失敗", entry.name);
                return false;
            }
        }
        return true;
    }

    // Downsample → BlurH → BlurV → Apply の 4 パスを宣言する。
    // 中間 2 枚は一時リソースなので、フレームをまたいで持ち越さない
    void LocalExposure::BuildPasses(PostEffectGraphBuilder& builder)
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

        const float lowResScale = 1.0f / static_cast<float>(kBaseDivisor);
        const PostEffectResourceRef logLum  = builder.CreateTransient(lowResScale, DXGI_FORMAT_R16_FLOAT);
        const PostEffectResourceRef blurredH = builder.CreateTransient(lowResScale, DXGI_FORMAT_R16_FLOAT);
        const PostEffectResourceRef blurredV = builder.CreateTransient(lowResScale, DXGI_FORMAT_R16_FLOAT);
        if (!logLum.IsValid() || !blurredH.IsValid() || !blurredV.IsValid()) {
            return;
        }

        builder.AddComputePass(
            "LocalExposure.Downsample", { builder.Input() }, logLum,
            [this](const PostEffectPassContext& passContext) { RecordDownsample(passContext); });

        builder.AddComputePass(
            "LocalExposure.BlurH", { logLum }, blurredH,
            [this](const PostEffectPassContext& passContext) { RecordBlur(passContext, /*horizontal*/ true); });

        builder.AddComputePass(
            "LocalExposure.BlurV", { blurredH }, blurredV,
            [this](const PostEffectPassContext& passContext) { RecordBlur(passContext, /*horizontal*/ false); });

        builder.AddComputePass(
            "LocalExposure.Apply", { builder.Input(), blurredV }, builder.ChainOutput(),
            [this](const PostEffectPassContext& passContext) { RecordApply(passContext); });
    }

    // ベース層の生成。1/8 解像度の対数輝度へ落とす
    void LocalExposure::RecordDownsample(const PostEffectPassContext& context)
    {
        if (!mappedDownsampleParams_ || context.reads.empty()) {
            return;
        }

        // 出力ターゲットの実寸法をベース層寸法の正とする（プールの丸めと食い違わないように）
        lowResWidth_  = context.width;
        lowResHeight_ = context.height;

        mappedDownsampleParams_->outputSize[0] = lowResWidth_;
        mappedDownsampleParams_->outputSize[1] = lowResHeight_;
        mappedDownsampleParams_->sourceSize[0] = baseWidth_;
        mappedDownsampleParams_->sourceSize[1] = baseHeight_;

        auto* cmdList = context.cmdList;
        cmdList->SetComputeRootSignature(downsamplePipeline_.GetComputeRootSignature());
        cmdList->SetPipelineState(downsamplePipeline_.GetComputePSO());

        const int textureIdx = downsamplePipeline_.GetComputeRootParamIndex("gTexture");
        const int outputIdx  = downsamplePipeline_.GetComputeRootParamIndex("gOutput");
        const int paramsIdx  = downsamplePipeline_.GetComputeRootParamIndex("DownsampleParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, context.reads[0]);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, context.output);
        if (paramsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(paramsIdx, downsampleParamsCB_->GetGPUVirtualAddress());

        cmdList->Dispatch(DispatchCount(lowResWidth_), DispatchCount(lowResHeight_), 1);
    }

    // 分離ガウス。横 → 縦の 2 回に分けることで、サンプル数が O(n^2) から O(n) になる
    void LocalExposure::RecordBlur(const PostEffectPassContext& context, bool horizontal)
    {
        BlurParams* params = horizontal ? mappedBlurHParams_ : mappedBlurVParams_;
        ID3D12Resource* paramsCB = horizontal ? blurHParamsCB_.Get() : blurVParamsCB_.Get();
        if (!params || context.reads.empty()) {
            return;
        }

        params->textureSize[0] = lowResWidth_;
        params->textureSize[1] = lowResHeight_;
        params->direction[0]   = horizontal ? 1u : 0u;
        params->direction[1]   = horizontal ? 0u : 1u;

        auto* cmdList = context.cmdList;
        cmdList->SetComputeRootSignature(blurPipeline_.GetComputeRootSignature());
        cmdList->SetPipelineState(blurPipeline_.GetComputePSO());

        const int sourceIdx = blurPipeline_.GetComputeRootParamIndex("gSource");
        const int outputIdx = blurPipeline_.GetComputeRootParamIndex("gOutput");
        const int paramsIdx = blurPipeline_.GetComputeRootParamIndex("BlurParams");

        if (sourceIdx >= 0) cmdList->SetComputeRootDescriptorTable(sourceIdx, context.reads[0]);
        if (outputIdx >= 0) cmdList->SetComputeRootDescriptorTable(outputIdx, context.output);
        if (paramsIdx >= 0) cmdList->SetComputeRootConstantBufferView(paramsIdx, paramsCB->GetGPUVirtualAddress());

        cmdList->Dispatch(DispatchCount(lowResWidth_), DispatchCount(lowResHeight_), 1);
    }

    // 適用パス。ぼかしたベース層をフル解像度輝度をガイドに再構成してからトーンを圧縮する
    // （素直に拡大するとエッジでハロー（縁の光り）が出る）
    void LocalExposure::RecordApply(const PostEffectPassContext& context)
    {
        if (!mappedApplyParams_ || context.reads.size() < 2) {
            return;
        }

        ApplyParams* params = mappedApplyParams_;
        params->screenSize[0]     = context.width;
        params->screenSize[1]     = context.height;
        params->baseSize[0]       = lowResWidth_;
        params->baseSize[1]       = lowResHeight_;
        params->highlightContrast = cvHighlightContrast.Get();
        params->shadowContrast    = cvShadowContrast.Get();
        params->detailStrength    = cvDetailStrength.Get();
        params->middleGreyBias    = cvMiddleGreyBias.Get();
        params->rangeSigma        = cvRangeSigma.Get();

        // 適用は基底が構築した PSO（GetComputeShaderPath が返す LocalExposure.CS.hlsl）を使う
        auto* cmdList = context.cmdList;
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        const int textureIdx = GetRootParamIndex("gTexture");
        const int baseIdx    = GetRootParamIndex("gBlurredLogLum");
        const int outputIdx  = GetRootParamIndex("gOutput");
        const int paramsIdx  = GetRootParamIndex("LocalExposureParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, context.reads[0]);
        if (baseIdx >= 0)    cmdList->SetComputeRootDescriptorTable(baseIdx, context.reads[1]);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, context.output);
        if (paramsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(paramsIdx, applyParamsCB_->GetGPUVirtualAddress());

        cmdList->Dispatch(DispatchCount(context.width), DispatchCount(context.height), 1);
    }

    void LocalExposure::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("LocalExposureParams");
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

    CVar<bool>* LocalExposure::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
