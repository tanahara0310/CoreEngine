#include "pch.h"
#include "DepthOfField.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Camera/View/ViewInfo.h"
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
            "r.DoF.Enabled", false,
            "被写界深度を有効にする（カットシーン/スクリーンショット演出用）",
            CVarRange{}, CVarFlags::NoUI };

        CVar<float> cvFocusDistance{
            "r.DoF.FocusDistance", 20.0f,
            "焦点面までの距離 [m]。この距離の物体が最もシャープになる",
            CVarRange{ 0.1f, 2000.0f } };

        CVar<float> cvFStop{
            "r.DoF.FStop", 2.8f,
            "絞り F 値。小さいほど被写界深度が浅い（ボケが強い）",
            CVarRange{ 0.7f, 22.0f } };

        CVar<float> cvFocalLength{
            "r.DoF.FocalLength", 50.0f,
            "レンズ焦点距離 [mm]（フルサイズセンサー 36mm 換算）。望遠ほどボケが強い",
            CVarRange{ 10.0f, 300.0f } };

        CVar<float> cvMaxCoc{
            "r.DoF.MaxCoC", 16.0f,
            "錯乱円の上限 [px]。大きいほど強くボケられるがギャザー品質が薄まる",
            CVarRange{ 2.0f, 32.0f } };

        CVar<int> cvSampleCount{
            "r.DoF.SampleCount", 48,
            "ギャザーのサンプル数。多いほど滑らかで重い",
            CVarRange{ 16.0f, 128.0f } };

        constexpr const char* kCVarPrefix = "r.DoF";

        /// @brief フルサイズセンサーの横幅 [mm]
        constexpr float kSensorWidthMm = 36.0f;

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

    void DepthOfField::OnCreateConstantBuffers()
    {
        auto* device = directXCommon_->GetDevice();

        CreateMappedCB(device, prefilterParamsCB_, mappedPrefilterParams_);
        CreateMappedCB(device, gatherParamsCB_, mappedGatherParams_);
        CreateMappedCB(device, compositeParamsCB_, mappedCompositeParams_);

        internalPipelinesReady_ = CreateInternalPipelines();
        if (!internalPipelinesReady_) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "DepthOfField: Prefilter/Gather パイプラインの構築に失敗（DoF は無効）");
        }
    }

    bool DepthOfField::CreateInternalPipelines()
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
            { prefilterPipeline_, prefilterProvider_, "Prefilter" },
            { gatherPipeline_,    gatherProvider_,    "Gather" },
        };

        for (Entry& entry : entries) {
            const bool built = entry.pipeline.Build(device, shaderCompiler, reflectionBuilder, entry.provider);
            if (!built || !entry.pipeline.HasComputePSO()) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "DepthOfField: {} パイプラインの構築に失敗", entry.name);
                return false;
            }
        }
        return true;
    }

    void DepthOfField::PrepareFrame(const PostEffectFrameContext& ctx)
    {
        if (ctx.view && ctx.view->isValid) {
            nearPlane_ = ctx.view->nearZ;
            farPlane_  = ctx.view->farZ;
        }
    }

    void DepthOfField::DeclareExtraInputs(std::vector<PostEffectInputBinding>& out) const
    {
        // 実際の読み取りは BuildPasses のパス reads で行う。ここは「無いフレームは
        // チェーンから外す」判定のための申告
        out.push_back({ "gDepth", FrameBlackboard::SceneDepth, /*required*/ true });
    }

    float DepthOfField::ComputeCocScalePx() const
    {
        // 薄レンズ: CoC(センサー上,m) = A・f/(fd - f) × (d - fd)/d
        //   A = f/N（開口径）、f = 焦点距離、fd = 焦点面距離（全て m）
        // これを (d-fd)/d に掛ける係数として前計算し、センサー幅で割って画素へ換算する
        const float f  = cvFocalLength.Get() * 0.001f; // mm → m
        const float fd = std::max(cvFocusDistance.Get(), f + 0.01f);
        const float aperture = f / cvFStop.Get();
        const float cocScaleMeters = aperture * f / (fd - f);
        return cocScaleMeters / (kSensorWidthMm * 0.001f) * static_cast<float>(fullWidth_);
    }

    void DepthOfField::BuildPasses(PostEffectGraphBuilder& builder)
    {
        // パイプラインが無いフレームは何もせず素通し
        if (!internalPipelinesReady_) {
            return;
        }

        fullWidth_  = builder.BaseWidth();
        fullHeight_ = builder.BaseHeight();
        if (fullWidth_ == 0 || fullHeight_ == 0) {
            return;
        }

        const PostEffectResourceRef prefiltered = builder.CreateTransient(0.5f);
        const PostEffectResourceRef gathered    = builder.CreateTransient(0.5f);
        if (!prefiltered.IsValid() || !gathered.IsValid()) {
            return;
        }

        builder.AddComputePass(
            "DoF.Prefilter",
            { builder.Input(), builder.Read(FrameBlackboard::SceneDepth) }, prefiltered,
            [this](const PostEffectPassContext& passContext) { RecordPrefilter(passContext); });

        builder.AddComputePass(
            "DoF.Gather",
            { prefiltered }, gathered,
            [this](const PostEffectPassContext& passContext) { RecordGather(passContext); });

        builder.AddComputePass(
            "DoF.Composite",
            { builder.Input(), gathered, builder.Read(FrameBlackboard::SceneDepth) },
            builder.ChainOutput(),
            [this](const PostEffectPassContext& passContext) { RecordComposite(passContext); });
    }

    void DepthOfField::RecordPrefilter(const PostEffectPassContext& context)
    {
        if (!mappedPrefilterParams_ || context.reads.size() < 2) {
            return;
        }

        // 出力ターゲットの実寸法をハーフ解像度の正とする
        halfWidth_  = context.width;
        halfHeight_ = context.height;

        PrefilterParams* params = mappedPrefilterParams_;
        params->outputSize[0] = halfWidth_;
        params->outputSize[1] = halfHeight_;
        params->fullSize[0]   = fullWidth_;
        params->fullSize[1]   = fullHeight_;
        params->focusDistance = cvFocusDistance.Get();
        params->cocScalePx    = ComputeCocScalePx();
        params->maxCocPx      = cvMaxCoc.Get();
        params->nearPlane     = nearPlane_;
        params->farPlane      = farPlane_;

        auto* cmdList = context.cmdList;
        cmdList->SetComputeRootSignature(prefilterPipeline_.GetComputeRootSignature());
        cmdList->SetPipelineState(prefilterPipeline_.GetComputePSO());

        const int textureIdx = prefilterPipeline_.GetComputeRootParamIndex("gTexture");
        const int depthIdx   = prefilterPipeline_.GetComputeRootParamIndex("gDepth");
        const int outputIdx  = prefilterPipeline_.GetComputeRootParamIndex("gOutput");
        const int paramsIdx  = prefilterPipeline_.GetComputeRootParamIndex("DoFPrefilterParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, context.reads[0]);
        if (depthIdx >= 0)   cmdList->SetComputeRootDescriptorTable(depthIdx, context.reads[1]);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, context.output);
        if (paramsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(paramsIdx, prefilterParamsCB_->GetGPUVirtualAddress());

        cmdList->Dispatch(DispatchCount(halfWidth_), DispatchCount(halfHeight_), 1);
    }

    void DepthOfField::RecordGather(const PostEffectPassContext& context)
    {
        if (!mappedGatherParams_ || context.reads.empty()) {
            return;
        }

        GatherParams* params = mappedGatherParams_;
        params->textureSize[0] = halfWidth_;
        params->textureSize[1] = halfHeight_;
        params->maxCocHalfPx   = cvMaxCoc.Get() * 0.5f;
        params->sampleCount    = static_cast<uint32_t>(std::max(1, cvSampleCount.Get()));

        auto* cmdList = context.cmdList;
        cmdList->SetComputeRootSignature(gatherPipeline_.GetComputeRootSignature());
        cmdList->SetPipelineState(gatherPipeline_.GetComputePSO());

        const int sourceIdx = gatherPipeline_.GetComputeRootParamIndex("gPrefiltered");
        const int outputIdx = gatherPipeline_.GetComputeRootParamIndex("gOutput");
        const int paramsIdx = gatherPipeline_.GetComputeRootParamIndex("DoFGatherParams");

        if (sourceIdx >= 0) cmdList->SetComputeRootDescriptorTable(sourceIdx, context.reads[0]);
        if (outputIdx >= 0) cmdList->SetComputeRootDescriptorTable(outputIdx, context.output);
        if (paramsIdx >= 0) cmdList->SetComputeRootConstantBufferView(paramsIdx, gatherParamsCB_->GetGPUVirtualAddress());

        cmdList->Dispatch(DispatchCount(halfWidth_), DispatchCount(halfHeight_), 1);
    }

    void DepthOfField::RecordComposite(const PostEffectPassContext& context)
    {
        if (!mappedCompositeParams_ || context.reads.size() < 3) {
            return;
        }

        CompositeParams* params = mappedCompositeParams_;
        params->fullSize[0]   = context.width;
        params->fullSize[1]   = context.height;
        params->halfSize[0]   = halfWidth_;
        params->halfSize[1]   = halfHeight_;
        params->focusDistance = cvFocusDistance.Get();
        params->cocScalePx    = ComputeCocScalePx();
        params->maxCocPx      = cvMaxCoc.Get();
        params->nearPlane     = nearPlane_;
        params->farPlane      = farPlane_;

        // 合成は基底が構築した PSO（GetComputeShaderPath が返す DoFComposite.CS.hlsl）を使う
        auto* cmdList = context.cmdList;
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        const int textureIdx = GetRootParamIndex("gTexture");
        const int blurredIdx = GetRootParamIndex("gBlurred");
        const int depthIdx   = GetRootParamIndex("gDepth");
        const int outputIdx  = GetRootParamIndex("gOutput");
        const int paramsIdx  = GetRootParamIndex("DoFCompositeParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, context.reads[0]);
        if (blurredIdx >= 0) cmdList->SetComputeRootDescriptorTable(blurredIdx, context.reads[1]);
        if (depthIdx >= 0)   cmdList->SetComputeRootDescriptorTable(depthIdx, context.reads[2]);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, context.output);
        if (paramsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(paramsIdx, compositeParamsCB_->GetGPUVirtualAddress());

        cmdList->Dispatch(DispatchCount(context.width), DispatchCount(context.height), 1);
    }

    void DepthOfField::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("DepthOfFieldParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        UI::Separator();

        CVarUI::DrawTree(kCVarPrefix);

        ImGui::TextDisabled("クリップ距離（カメラから自動設定）: near %.2f / far %.1f",
                            nearPlane_, farPlane_);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* DepthOfField::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
