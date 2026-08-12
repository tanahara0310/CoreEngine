#include "pch.h"
#include "MotionBlur.h"
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
            "r.MotionBlur.Enabled", true,
            "モーションブラーを有効にする",
            CVarRange{}, CVarFlags::NoUI };

        CVar<float> cvShutterAngle{
            "r.MotionBlur.ShutterAngle", 180.0f,
            "シャッター開角度[度]。180 で露光時間がフレームの半分（映画の標準）。大きいほどブレる",
            CVarRange{ 0.0f, 360.0f } };

        CVar<float> cvMaxBlurPixels{
            "r.MotionBlur.MaxBlurPixels", 20.0f,
            "ブラーの最大到達距離[px]。タイル一辺（20px）を超える値は探索範囲外になるため上限で切る",
            CVarRange{ 1.0f, static_cast<float>(MotionBlur::kTileSize) } };

        CVar<int> cvSampleCount{
            "r.MotionBlur.SampleCount", 12,
            "ブラー方向の探索サンプル数。多いほど滑らかで重い",
            CVarRange{ 4.0f, 32.0f } };

        CVar<float> cvDepthExtent{
            "r.MotionBlur.DepthExtent", 1.0f,
            "前後判定のソフト境界幅[m]。小さいほど前景/背景の分離が鋭くなる",
            CVarRange{ 0.01f, 50.0f } };

        constexpr const char* kCVarPrefix = "r.MotionBlur";

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

    void MotionBlur::OnCreateConstantBuffers()
    {
        auto* device = directXCommon_->GetDevice();

        CreateMappedCB(device, tileMaxParamsCB_, mappedTileMaxParams_);
        CreateMappedCB(device, neighborMaxParamsCB_, mappedNeighborMaxParams_);
        CreateMappedCB(device, gatherParamsCB_, mappedGatherParams_);

        internalPipelinesReady_ = CreateInternalPipelines();
        if (!internalPipelinesReady_) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "MotionBlur: TileMax/NeighborMax パイプラインの構築に失敗（モーションブラーは無効）");
        }
    }

    bool MotionBlur::CreateInternalPipelines()
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
            { tileMaxPipeline_,     tileMaxProvider_,     "TileMax" },
            { neighborMaxPipeline_, neighborMaxProvider_, "NeighborMax" },
        };

        for (Entry& entry : entries) {
            const bool built = entry.pipeline.Build(device, shaderCompiler, reflectionBuilder, entry.provider);
            if (!built || !entry.pipeline.HasComputePSO()) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "MotionBlur: {} パイプラインの構築に失敗", entry.name);
                return false;
            }
        }
        return true;
    }

    void MotionBlur::PrepareFrame(const PostEffectFrameContext& ctx)
    {
        // 深度の線形化には描画に使われたカメラと同じ near/far が要る。
        // ビューが未確定のフレームは前回値を維持する（0 で割る事故を避ける）
        if (ctx.view && ctx.view->isValid) {
            nearPlane_ = ctx.view->nearZ;
            farPlane_  = ctx.view->farZ;
        }
    }

    void MotionBlur::DeclareExtraInputs(std::vector<PostEffectInputBinding>& out) const
    {
        // 実際の読み取りは BuildPasses が積むパスの reads で行う（バリアはそちらで導出される）。
        // ここでの申告は「必須入力が無いフレームはチェーンから外す」判定のためのもの
        out.push_back({ "gVelocity", FrameBlackboard::GBufferMotionVector, /*required*/ true });
        out.push_back({ "gDepth",    FrameBlackboard::SceneDepth,          /*required*/ true });
    }

    void MotionBlur::BuildPasses(PostEffectGraphBuilder& builder)
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

        // タイルバッファは 1/kTileSize 解像度。端数タイルは切り捨てられるが、
        // ギャザー側がタイル座標をクランプするので右端/下端は隣のタイル値で代用される
        const float tileScale = 1.0f / static_cast<float>(kTileSize);
        const PostEffectResourceRef tileMax     = builder.CreateTransient(tileScale, DXGI_FORMAT_R16G16_FLOAT);
        const PostEffectResourceRef neighborMax = builder.CreateTransient(tileScale, DXGI_FORMAT_R16G16_FLOAT);
        if (!tileMax.IsValid() || !neighborMax.IsValid()) {
            return;
        }

        builder.AddComputePass(
            "MotionBlur.TileMax",
            { builder.Read(FrameBlackboard::GBufferMotionVector) }, tileMax,
            [this](const PostEffectPassContext& passContext) { RecordTileMax(passContext); });

        builder.AddComputePass(
            "MotionBlur.NeighborMax",
            { tileMax }, neighborMax,
            [this](const PostEffectPassContext& passContext) { RecordNeighborMax(passContext); });

        builder.AddComputePass(
            "MotionBlur.Gather",
            { builder.Input(),
              builder.Read(FrameBlackboard::GBufferMotionVector),
              builder.Read(FrameBlackboard::SceneDepth),
              neighborMax },
            builder.ChainOutput(),
            [this](const PostEffectPassContext& passContext) { RecordGather(passContext); });
    }

    void MotionBlur::RecordTileMax(const PostEffectPassContext& context)
    {
        if (!mappedTileMaxParams_ || context.reads.empty()) {
            return;
        }

        // 出力ターゲットの実寸法をタイル数の正とする（プールの丸めと食い違わないように）
        tileCountX_ = context.width;
        tileCountY_ = context.height;

        TileMaxParams* params = mappedTileMaxParams_;
        params->screenSize[0]   = baseWidth_;
        params->screenSize[1]   = baseHeight_;
        params->tileCount[0]    = tileCountX_;
        params->tileCount[1]    = tileCountY_;
        params->shutterFraction = cvShutterAngle.Get() / 360.0f;
        params->maxBlurPixels   = cvMaxBlurPixels.Get();
        params->tileSize        = kTileSize;

        auto* cmdList = context.cmdList;
        cmdList->SetComputeRootSignature(tileMaxPipeline_.GetComputeRootSignature());
        cmdList->SetPipelineState(tileMaxPipeline_.GetComputePSO());

        const int velocityIdx = tileMaxPipeline_.GetComputeRootParamIndex("gVelocity");
        const int outputIdx   = tileMaxPipeline_.GetComputeRootParamIndex("gOutput");
        const int paramsIdx   = tileMaxPipeline_.GetComputeRootParamIndex("TileMaxParams");

        if (velocityIdx >= 0) cmdList->SetComputeRootDescriptorTable(velocityIdx, context.reads[0]);
        if (outputIdx >= 0)   cmdList->SetComputeRootDescriptorTable(outputIdx, context.output);
        if (paramsIdx >= 0)   cmdList->SetComputeRootConstantBufferView(paramsIdx, tileMaxParamsCB_->GetGPUVirtualAddress());

        cmdList->Dispatch(DispatchCount(tileCountX_), DispatchCount(tileCountY_), 1);
    }

    void MotionBlur::RecordNeighborMax(const PostEffectPassContext& context)
    {
        if (!mappedNeighborMaxParams_ || context.reads.empty()) {
            return;
        }

        NeighborMaxParams* params = mappedNeighborMaxParams_;
        params->tileCount[0] = tileCountX_;
        params->tileCount[1] = tileCountY_;

        auto* cmdList = context.cmdList;
        cmdList->SetComputeRootSignature(neighborMaxPipeline_.GetComputeRootSignature());
        cmdList->SetPipelineState(neighborMaxPipeline_.GetComputePSO());

        const int tileMaxIdx = neighborMaxPipeline_.GetComputeRootParamIndex("gTileMax");
        const int outputIdx  = neighborMaxPipeline_.GetComputeRootParamIndex("gOutput");
        const int paramsIdx  = neighborMaxPipeline_.GetComputeRootParamIndex("NeighborMaxParams");

        if (tileMaxIdx >= 0) cmdList->SetComputeRootDescriptorTable(tileMaxIdx, context.reads[0]);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, context.output);
        if (paramsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(paramsIdx, neighborMaxParamsCB_->GetGPUVirtualAddress());

        cmdList->Dispatch(DispatchCount(tileCountX_), DispatchCount(tileCountY_), 1);
    }

    void MotionBlur::RecordGather(const PostEffectPassContext& context)
    {
        if (!mappedGatherParams_ || context.reads.size() < 4) {
            return;
        }

        GatherParams* params = mappedGatherParams_;
        params->screenSize[0]   = context.width;
        params->screenSize[1]   = context.height;
        params->tileCount[0]    = tileCountX_;
        params->tileCount[1]    = tileCountY_;
        params->shutterFraction = cvShutterAngle.Get() / 360.0f;
        params->maxBlurPixels   = cvMaxBlurPixels.Get();
        params->sampleCount     = static_cast<uint32_t>(std::max(1, cvSampleCount.Get()));
        params->tileSize        = kTileSize;
        params->nearPlane       = nearPlane_;
        params->farPlane        = farPlane_;
        params->depthExtent     = cvDepthExtent.Get();

        // ギャザーは基底が構築した PSO（GetComputeShaderPath が返す MotionBlur.CS.hlsl）を使う
        auto* cmdList = context.cmdList;
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        const int textureIdx     = GetRootParamIndex("gTexture");
        const int velocityIdx    = GetRootParamIndex("gVelocity");
        const int depthIdx       = GetRootParamIndex("gDepth");
        const int neighborMaxIdx = GetRootParamIndex("gNeighborMax");
        const int outputIdx      = GetRootParamIndex("gOutput");
        const int paramsIdx      = GetRootParamIndex("MotionBlurParams");

        if (textureIdx >= 0)     cmdList->SetComputeRootDescriptorTable(textureIdx, context.reads[0]);
        if (velocityIdx >= 0)    cmdList->SetComputeRootDescriptorTable(velocityIdx, context.reads[1]);
        if (depthIdx >= 0)       cmdList->SetComputeRootDescriptorTable(depthIdx, context.reads[2]);
        if (neighborMaxIdx >= 0) cmdList->SetComputeRootDescriptorTable(neighborMaxIdx, context.reads[3]);
        if (outputIdx >= 0)      cmdList->SetComputeRootDescriptorTable(outputIdx, context.output);
        if (paramsIdx >= 0)      cmdList->SetComputeRootConstantBufferView(paramsIdx, gatherParamsCB_->GetGPUVirtualAddress());

        cmdList->Dispatch(DispatchCount(context.width), DispatchCount(context.height), 1);
    }

    void MotionBlur::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("MotionBlurParams");
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

    CVar<bool>* MotionBlur::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
