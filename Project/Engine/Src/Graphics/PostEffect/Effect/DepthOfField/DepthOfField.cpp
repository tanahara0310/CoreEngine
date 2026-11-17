#include "pch.h"
#include "DepthOfField.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Camera/View/ViewInfo.h"
#include "Utility/CVar/CVar.h"
#include "Utility/Logger/Logger.h"
#ifdef CORE_EDITOR
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <algorithm>


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
    }

    void DepthOfField::OnCreateResources()
    {
        internalPipelinesReady_ = BuildInternalPipelines({
            { prefilterPipeline_, L"DoFPrefilter.CS.hlsl", "Prefilter" },
            { gatherPipeline_,    L"DoFGather.CS.hlsl",    "Gather" },
        });
        if (!internalPipelinesReady_) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "DepthOfField: Prefilter/Gather パイプラインの構築に失敗（DoF は無効）");
        }
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
        if (context.reads.size() < 2) {
            return;
        }

        // 出力ターゲットの実寸法をハーフ解像度の正とする
        halfWidth_  = context.width;
        halfHeight_ = context.height;

        PrefilterParams params{};
        params.outputSize[0] = halfWidth_;
        params.outputSize[1] = halfHeight_;
        params.fullSize[0]   = fullWidth_;
        params.fullSize[1]   = fullHeight_;
        params.focusDistance = cvFocusDistance.Get();
        params.cocScalePx    = ComputeCocScalePx();
        params.maxCocPx      = cvMaxCoc.Get();
        params.nearPlane     = nearPlane_;
        params.farPlane      = farPlane_;

        DispatchPass(context.cmdList, prefilterPipeline_,
            { { "gTexture", context.reads[0] }, { "gDepth", context.reads[1] }, { "gOutput", context.output } },
            { { "DoFPrefilterParams", UploadConstants(params) } },
            halfWidth_, halfHeight_);
    }

    void DepthOfField::RecordGather(const PostEffectPassContext& context)
    {
        if (context.reads.empty()) {
            return;
        }

        GatherParams params{};
        params.textureSize[0] = halfWidth_;
        params.textureSize[1] = halfHeight_;
        params.maxCocHalfPx   = cvMaxCoc.Get() * 0.5f;
        params.sampleCount    = static_cast<uint32_t>(std::max(1, cvSampleCount.Get()));

        DispatchPass(context.cmdList, gatherPipeline_,
            { { "gPrefiltered", context.reads[0] }, { "gOutput", context.output } },
            { { "DoFGatherParams", UploadConstants(params) } },
            halfWidth_, halfHeight_);
    }

    void DepthOfField::RecordComposite(const PostEffectPassContext& context)
    {
        if (context.reads.size() < 3) {
            return;
        }

        CompositeParams params{};
        params.fullSize[0]   = context.width;
        params.fullSize[1]   = context.height;
        params.halfSize[0]   = halfWidth_;
        params.halfSize[1]   = halfHeight_;
        params.focusDistance = cvFocusDistance.Get();
        params.cocScalePx    = ComputeCocScalePx();
        params.maxCocPx      = cvMaxCoc.Get();
        params.nearPlane     = nearPlane_;
        params.farPlane      = farPlane_;

        // 合成は基底が構築した PSO（GetComputeShaderPath が返す DoFComposite.CS.hlsl）を使う
        DispatchPass(context.cmdList,
            { { "gTexture", context.reads[0] }, { "gBlurred", context.reads[1] },
              { "gDepth", context.reads[2] }, { "gOutput", context.output } },
            { { "DoFCompositeParams", UploadConstants(params) } },
            context.width, context.height);
    }

    void DepthOfField::DrawImGui()
    {
#ifdef CORE_EDITOR
        BeginEffectImGui("DepthOfFieldParams");
        UI::Separator();

        CVarUI::DrawTree(kCVarPrefix);

        ImGui::TextDisabled("クリップ距離（カメラから自動設定）: near %.2f / far %.1f",
                            nearPlane_, farPlane_);

        DrawResetDefaultsImGui(kCVarPrefix);
        EndEffectImGui();
#endif // CORE_EDITOR
    }

    CVar<bool>* DepthOfField::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
