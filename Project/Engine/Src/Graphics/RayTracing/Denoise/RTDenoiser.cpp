#include "pch.h"
#include "RTDenoiser.h"

#include <algorithm>
#include <cassert>
#include <string>

#include "Camera/View/ViewInfo.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/Denoise/RTDenoiseBindings.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ShaderProgram.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    // =========================================================================
    // HLSL の cbuffer と並びを合わせる構造体（どれもスカラーだけで作る）
    // =========================================================================

    /// @brief RTDenoiseTemporal.CS.hlsl の RTDenoiseTemporalConstants
    struct RTDenoiseTemporalConstants {
        int   traceWidth;       // offset  0
        int   traceHeight;      // offset  4
        float maxHistoryFrames; // offset  8
        float disableHistory;   // offset 12 → row1 終了(16)
        float projM33;          // offset 16
        float projM43;          // offset 20
        int   traceScale;       // offset 24
        int   pad0;             // offset 28 → row2 終了(32)
        int   pad1;             // offset 32
        int   fullWidth;        // offset 36
        int   fullHeight;       // offset 40
        float clampSigmaScale;  // offset 44 → row3 終了(48)
        float clampMargin;      // offset 48
        float depthTolerance;   // offset 52
        float stillnessSigma;   // offset 56
        int   pad2;             // offset 60 → row4 終了(64)
    };
    static_assert(sizeof(RTDenoiseTemporalConstants) == 64, "RTDenoiseTemporalConstants size mismatch with HLSL cbuffer");

    static constexpr Cb::Field kRTDenoiseTemporalConstantsFields[] = {
        CB_FIELD(RTDenoiseTemporalConstants, traceWidth), CB_FIELD(RTDenoiseTemporalConstants, traceHeight),
        CB_FIELD(RTDenoiseTemporalConstants, maxHistoryFrames), CB_FIELD(RTDenoiseTemporalConstants, disableHistory),
        CB_FIELD(RTDenoiseTemporalConstants, projM33), CB_FIELD(RTDenoiseTemporalConstants, projM43),
        CB_FIELD(RTDenoiseTemporalConstants, traceScale), CB_FIELD(RTDenoiseTemporalConstants, pad0),
        CB_FIELD(RTDenoiseTemporalConstants, pad1), CB_FIELD(RTDenoiseTemporalConstants, fullWidth),
        CB_FIELD(RTDenoiseTemporalConstants, fullHeight), CB_FIELD(RTDenoiseTemporalConstants, clampSigmaScale),
        CB_FIELD(RTDenoiseTemporalConstants, clampMargin), CB_FIELD(RTDenoiseTemporalConstants, depthTolerance),
        CB_FIELD(RTDenoiseTemporalConstants, stillnessSigma), CB_FIELD(RTDenoiseTemporalConstants, pad2),
    };
    CB_VERIFY_LAYOUT(RTDenoiseTemporalConstants, kRTDenoiseTemporalConstantsFields);
    CB_BIND_HLSL(RTDenoiseTemporalConstants, kRTDenoiseTemporalConstantsFields, "RTDenoiseTemporalConstants");

    /// @brief RTDenoiseATrous.CS.hlsl の RTDenoiseATrousConstants
    struct RTDenoiseATrousConstants {
        int   stepSize;    // offset  0
        float phiValue;    // offset  4
        float phiNormal;   // offset  8
        float phiDepth;    // offset 12 → row1 終了(16)
        int   traceWidth;  // offset 16
        int   traceHeight; // offset 20
        float projM33;     // offset 24
        float projM43;     // offset 28 → row2 終了(32)
        int   traceScale;  // offset 32
        int   pad0;        // offset 36
        int   pad1;        // offset 40
        int   fullWidth;   // offset 44 → row3 終了(48)
        int   fullHeight;  // offset 48
        int   pad2;        // offset 52
        int   pad3;        // offset 56
        int   pad4;        // offset 60 → row4 終了(64)
    };
    static_assert(sizeof(RTDenoiseATrousConstants) == 64, "RTDenoiseATrousConstants size mismatch with HLSL cbuffer");

    static constexpr Cb::Field kRTDenoiseATrousConstantsFields[] = {
        CB_FIELD(RTDenoiseATrousConstants, stepSize), CB_FIELD(RTDenoiseATrousConstants, phiValue),
        CB_FIELD(RTDenoiseATrousConstants, phiNormal), CB_FIELD(RTDenoiseATrousConstants, phiDepth),
        CB_FIELD(RTDenoiseATrousConstants, traceWidth), CB_FIELD(RTDenoiseATrousConstants, traceHeight),
        CB_FIELD(RTDenoiseATrousConstants, projM33), CB_FIELD(RTDenoiseATrousConstants, projM43),
        CB_FIELD(RTDenoiseATrousConstants, traceScale), CB_FIELD(RTDenoiseATrousConstants, pad0),
        CB_FIELD(RTDenoiseATrousConstants, pad1), CB_FIELD(RTDenoiseATrousConstants, fullWidth),
        CB_FIELD(RTDenoiseATrousConstants, fullHeight), CB_FIELD(RTDenoiseATrousConstants, pad2),
        CB_FIELD(RTDenoiseATrousConstants, pad3), CB_FIELD(RTDenoiseATrousConstants, pad4),
    };
    CB_VERIFY_LAYOUT(RTDenoiseATrousConstants, kRTDenoiseATrousConstantsFields);
    CB_BIND_HLSL(RTDenoiseATrousConstants, kRTDenoiseATrousConstantsFields, "RTDenoiseATrousConstants");

    /// @brief RTDenoiseResolve.CS.hlsl の RTDenoiseResolveConstants
    struct RTDenoiseResolveConstants {
        int   fullWidth;   // offset  0
        int   fullHeight;  // offset  4
        int   traceWidth;  // offset  8
        int   traceHeight; // offset 12 → row1 終了(16)
        float projM33;     // offset 16
        float projM43;     // offset 20
        int   traceScale;  // offset 24
        int   pad0;        // offset 28 → row2 終了(32)
        int   pad1;        // offset 32
        float phiDepth;    // offset 36
        int   pad2;        // offset 40
        int   pad3;        // offset 44 → row3 終了(48)
    };
    static_assert(sizeof(RTDenoiseResolveConstants) == 48, "RTDenoiseResolveConstants size mismatch with HLSL cbuffer");

    static constexpr Cb::Field kRTDenoiseResolveConstantsFields[] = {
        CB_FIELD(RTDenoiseResolveConstants, fullWidth), CB_FIELD(RTDenoiseResolveConstants, fullHeight),
        CB_FIELD(RTDenoiseResolveConstants, traceWidth), CB_FIELD(RTDenoiseResolveConstants, traceHeight),
        CB_FIELD(RTDenoiseResolveConstants, projM33), CB_FIELD(RTDenoiseResolveConstants, projM43),
        CB_FIELD(RTDenoiseResolveConstants, traceScale), CB_FIELD(RTDenoiseResolveConstants, pad0),
        CB_FIELD(RTDenoiseResolveConstants, pad1), CB_FIELD(RTDenoiseResolveConstants, phiDepth),
        CB_FIELD(RTDenoiseResolveConstants, pad2), CB_FIELD(RTDenoiseResolveConstants, pad3),
    };
    CB_VERIFY_LAYOUT(RTDenoiseResolveConstants, kRTDenoiseResolveConstantsFields);
    CB_BIND_HLSL(RTDenoiseResolveConstants, kRTDenoiseResolveConstantsFields, "RTDenoiseResolveConstants");

    namespace
    {
        /// @brief 投影行列から深度の線形化に使う 2 つの要素を取り出す（ndcZ = _33 + _43 / viewZ）
        void ExtractProjectionZW(const Matrix4x4& projection, float& outM33, float& outM43)
        {
            outM33 = projection.m[2][2];
            outM43 = projection.m[3][2];
        }

        /// @brief 8x8 のスレッドグループで要るグループの数
        UINT DispatchGroupCount(UINT pixels) { return (pixels + 7) / 8; }
    }

    bool BuildRTDenoiseGuides(const RenderContext& context, RTDenoiseGuides& out)
    {
        if (!context.gBufferManager || !context.frameViews) {
            return false;
        }
        const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);
        if (!view.isValid) {
            return false;
        }
        out = {};
        if (context.frameBlackboard) {
            context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneDepth, out.sceneDepthSRV);
        }
        out.normalRoughnessSRV = context.gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness);
        out.motionVectorSRV = context.gBufferManager->GetSRVHandle(GBufferManager::Target::MotionVector);
        out.projection = view.projection;
        return true;
    }

    // =========================================================================
    // 初期化
    // =========================================================================
    bool RTDenoiser::Initialize(const RTEffectInitContext& init, const char* ownerName, uint32_t channelCount)
    {
        assert(channelCount <= kMaxChannels && "RTDenoiser: チャンネルの数が上限を超えている");
        dxCommon_ = init.dxCommon;
        descriptorAllocator_ = init.descriptorAllocator;
        shaderProgramCache_ = init.shaderProgramCache;
        ownerName_ = ownerName;
        channelCount_ = (std::min)(channelCount, kMaxChannels);

        const bool temporal = CreateComputePass(L"RTDenoiseTemporal.CS.hlsl",
            RTDenoiseTemporalBind::kDecls, RTDenoiseTemporalBind::Slot::Count,
            "RTDenoiseTemporalConstants", "DenoiseTemporal", temporalPass_);
        const bool atrous = CreateComputePass(L"RTDenoiseATrous.CS.hlsl",
            RTDenoiseATrousBind::kDecls, RTDenoiseATrousBind::Slot::Count,
            "RTDenoiseATrousConstants", "DenoiseATrous", atrousPass_);
        const bool resolve = CreateComputePass(L"RTDenoiseResolve.CS.hlsl",
            RTDenoiseResolveBind::kDecls, RTDenoiseResolveBind::Slot::Count,
            "RTDenoiseResolveConstants", "DenoiseResolve", resolvePass_);
        return temporal && atrous && resolve;
    }

    bool RTDenoiser::CreateComputePass(const wchar_t* shaderPath, const ShaderBindingDecl* decls, size_t declCount,
        const char* rootConstantName, const char* passName, ComputePass& outPass)
    {
        Logger& log = Logger::GetInstance();
        const std::string label = std::string(ownerName_) + "_" + passName;

        const ShaderProgram* program = shaderProgramCache_
            ? shaderProgramCache_->GetOrCreateCompute(shaderPath, label.c_str(), L"cs_6_6") : nullptr;
        if (!program) {
            log.Logf(LogLevel::Warn, LogCategory::Graphics, "RTDenoiser: {} shader compile failed", label);
            return false;
        }

        // 定数だけルート定数にする（DWORD の数はリフレクションが cbuffer の大きさから決める）
        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);
        config.ConfigureResource(ResourceBindingConfig(rootConstantName, BindingStrategy::RootConstants));

        const auto buildResult = outPass.rootSigMgr.Build(dxCommon_->GetDevice(), program->GetReflection(), config);
        if (!buildResult.success) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "RTDenoiser: {} のルートシグネチャ構築に失敗: {}", label, buildResult.errorMessage);
            return false;
        }

        // 宣言表とシェーダの実体を突き合わせる
        try {
            outPass.bindings = BindingTable::Resolve(program->GetReflection(), decls, declCount, label.c_str());
        }
        catch (const std::exception& e) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "RTDenoiser: {} のバインド契約違反: {}", label, e.what());
            return false;
        }

        outPass.pipelineState = ComputePipelineUtil::Create(
            dxCommon_->GetDevice(), outPass.GetRootSignature(), program->GetCS(), label);
        if (!outPass.pipelineState) {
            return false;
        }

        outPass.initialized = true;
        log.Logf(LogLevel::Info, LogCategory::Graphics, "RTDenoiser: {} pipeline created", label);
        return true;
    }

    // =========================================================================
    // テクスチャ
    // =========================================================================
    bool RTDenoiser::EnsureTextures(uint32_t channel, UINT width, UINT height, UINT traceScale)
    {
        Channel& state = channels_[channel];
        const UINT traceWidth = (width + traceScale - 1) / traceScale;
        const UINT traceHeight = (height + traceScale - 1) / traceScale;
        if (width == state.width && height == state.height
            && traceWidth == state.traceWidth && traceHeight == state.traceHeight
            && textures_.HasTexture(SlotIndex(channel, Texture::Output))) {
            return true;
        }

        state.width = width;
        state.height = height;
        state.traceWidth = traceWidth;
        state.traceHeight = traceHeight;
        state.traceScale = traceScale;
        // 大きさが変わったら前の大きさの履歴は使わない
        state.historyValid = false;

        struct TextureDesc { Texture texture; const char* name; DXGI_FORMAT format; bool fullResolution; };
        static constexpr TextureDesc kTextures[] = {
            { Texture::Output,   "Output",   kOutputFormat, true },
            { Texture::Input,    "Input",    kInputFormat,  false },
            { Texture::FilterA,  "FilterA",  kSignalFormat, false },
            { Texture::FilterB,  "FilterB",  kSignalFormat, false },
            { Texture::HistoryA, "HistoryA", kSignalFormat, false },
            { Texture::HistoryB, "HistoryB", kSignalFormat, false },
        };

        RayTracingOutputViewSet::TextureOptions options{};
        options.allowUAV = true;
        options.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        for (const TextureDesc& desc : kTextures) {
            options.format = desc.format;
            const std::string name = std::string(ownerName_) + "_" + desc.name + "_c" + std::to_string(channel);
            if (!textures_.EnsureTexture(dxCommon_, descriptorAllocator_,
                desc.fullResolution ? width : traceWidth, desc.fullResolution ? height : traceHeight,
                SlotIndex(channel, desc.texture), ownerName_, name, options)) {
                return false;
            }
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
            "RTDenoiser: {} channel {} textures created (output {}x{} / trace {}x{})",
            ownerName_, channel, width, height, traceWidth, traceHeight);
        return true;
    }

    void RTDenoiser::OnResize(UINT width, UINT height, UINT traceScale)
    {
        if (width == 0 || height == 0) {
            return;
        }
        for (uint32_t channel = 0; channel < channelCount_; ++channel) {
            if (textures_.HasTexture(SlotIndex(channel, Texture::Output))) {
                EnsureTextures(channel, width, height, traceScale);
            }
        }
    }

    int RTDenoiser::ClampAtrousPassCount(int passCount)
    {
        return std::clamp(passCount, 0, kMaxAtrousPassCount);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RTDenoiser::GetPreviousHistorySRV(uint32_t channel) const
    {
        return TextureSRV(channel, channels_[channel].PreviousHistory());
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RTDenoiser::GetHistorySRV(uint32_t channel) const
    {
        return TextureSRV(channel, channels_[channel].CurrentHistory());
    }

    GpuResource& RTDenoiser::GetHistory(uint32_t channel)
    {
        return TextureResource(channel, channels_[channel].CurrentHistory());
    }

    // =========================================================================
    // フレームごとの処理
    // =========================================================================
    bool RTDenoiser::Prepare(uint32_t channel, UINT width, UINT height, UINT traceScale, uint64_t signalKey)
    {
        assert(channel < channelCount_ && "RTDenoiser: チャンネルの番号が範囲の外");
        if (!EnsureTextures(channel, width, height, traceScale)) {
            return false;
        }

        // 信号の出どころが変わったとき・前のフレームに準備していないときは履歴を捨てる
        Channel& state = channels_[channel];
        const uint64_t frameNumber = dxCommon_->Frame().FrameNumber();
        const bool continuous =
            (frameNumber == state.lastPrepareFrame) || (frameNumber == state.lastPrepareFrame + 1);
        if (state.signalKey != signalKey || !continuous) {
            state.historyValid = false;
        }
        state.signalKey = signalKey;
        state.lastPrepareFrame = frameNumber;

        // 履歴の書き込み先を入れ替える
        state.historyParity ^= 1u;
        return true;
    }

    void RTDenoiser::BeginInputWrite(ID3D12GraphicsCommandList* cmdList, uint32_t channel)
    {
        BarrierBatch batch(cmdList);
        batch.Transition(TextureResource(channel, Texture::Input), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        batch.Transition(TextureResource(channel, channels_[channel].PreviousHistory()),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void RTDenoiser::EndInputWrite(ID3D12GraphicsCommandList* cmdList, uint32_t channel)
    {
        GpuResource& input = TextureResource(channel, Texture::Input);
        BarrierBatch batch(cmdList);
        batch.UAV(input);
        batch.Transition(input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        batch.Flush();
    }

    void RTDenoiser::Accumulate(ID3D12GraphicsCommandList* cmdList, uint32_t channel,
        const RTDenoiseGuides& guides, const RTDenoiseSettings& settings)
    {
        if (!temporalPass_.initialized) {
            return;
        }

        Channel& state = channels_[channel];
        const Texture currentHistory = state.CurrentHistory();
        const Texture previousHistory = state.PreviousHistory();

        GpuResource& inputRes = TextureResource(channel, Texture::Input);
        GpuResource& prevRes = TextureResource(channel, previousHistory);
        GpuResource& curRes = TextureResource(channel, currentHistory);
        if (!inputRes.IsValid() || !prevRes.IsValid() || !curRes.IsValid()) {
            return;
        }

        {
            BarrierBatch batch(cmdList);
            batch.Transition(inputRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Transition(prevRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Transition(curRes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        cmdList->SetComputeRootSignature(temporalPass_.GetRootSignature());
        cmdList->SetPipelineState(temporalPass_.pipelineState.Get());

        namespace Bind = RTDenoiseTemporalBind;
        const BindingTable& table = temporalPass_.bindings;
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(table[Bind::gRawSignal], TextureSRV(channel, Texture::Input));
        binder.Set(table[Bind::gGBufferNormal], guides.normalRoughnessSRV);
        binder.Set(table[Bind::gGBufferDepth], guides.sceneDepthSRV);
        binder.Set(table[Bind::gHistorySignal], TextureSRV(channel, previousHistory));
        binder.Set(table[Bind::gMotionVector], guides.motionVectorSRV);
        binder.Set(table[Bind::gOutputSignal], TextureUAV(channel, currentHistory));

        RTDenoiseTemporalConstants tc{};
        tc.traceWidth = static_cast<int>(state.traceWidth);
        tc.traceHeight = static_cast<int>(state.traceHeight);
        tc.maxHistoryFrames = static_cast<float>(settings.maxHistoryFrames);
        tc.disableHistory = (state.historyValid && settings.useHistory) ? 0.0f : 1.0f;
        ExtractProjectionZW(guides.projection, tc.projM33, tc.projM43);
        tc.traceScale = static_cast<int>(state.traceScale);
        tc.fullWidth = static_cast<int>(state.width);
        tc.fullHeight = static_cast<int>(state.height);
        tc.clampSigmaScale = settings.historyClampSigma;
        tc.clampMargin = settings.historyClampMargin;
        tc.depthTolerance = settings.historyDepthTolerance;
        tc.stillnessSigma = settings.stillnessSigma;
        binder.SetConstants(table[Bind::RTDenoiseTemporalConstants], tc);

        binder.ValidateBeforeDraw(table);
        cmdList->Dispatch(DispatchGroupCount(state.traceWidth), DispatchGroupCount(state.traceHeight), 1);

        // A-Trous が読むので書き込みの完了を待ってから読み取りへ
        BarrierBatch curToSrv(cmdList);
        curToSrv.UAV(curRes);
        curToSrv.Transition(curRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        curToSrv.Flush();

        state.historyValid = true;
    }

    void RTDenoiser::Filter(ID3D12GraphicsCommandList* cmdList, uint32_t channel,
        const RTDenoiseGuides& guides, const RTDenoiseSettings& settings)
    {
        static constexpr int kSteps[kMaxAtrousPassCount] = { 1, 2, 4, 8 };

        Channel& state = channels_[channel];
        const int numPasses = atrousPass_.initialized ? ClampAtrousPassCount(settings.atrousPassCount) : 0;

        // A-Trous は 4 チャンネルの信号を読み書きするので、最後は必ず仕上げでフル解像度の出力へ書く
        Texture lastTexture = state.CurrentHistory();

        if (numPasses > 0) {
            cmdList->SetComputeRootSignature(atrousPass_.GetRootSignature());
            cmdList->SetPipelineState(atrousPass_.pipelineState.Get());

            namespace Bind = RTDenoiseATrousBind;
            const BindingTable& table = atrousPass_.bindings;
            const UINT groupX = DispatchGroupCount(state.traceWidth);
            const UINT groupY = DispatchGroupCount(state.traceHeight);

            for (int pass = 0; pass < numPasses; ++pass) {
                const Texture inTexture = lastTexture;
                const Texture outTexture = (pass % 2 == 0) ? Texture::FilterA : Texture::FilterB;

                GpuResource& inputRes = TextureResource(channel, inTexture);
                GpuResource& outputRes = TextureResource(channel, outTexture);
                if (!inputRes.IsValid() || !outputRes.IsValid()) {
                    return;
                }

                {
                    BarrierBatch batch(cmdList);
                    batch.Transition(inputRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    batch.Transition(outputRes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                }

                // 差し忘れの検出をパス単位で見るため、バインダーはパスごとに作り直す
                ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
                binder.Set(table[Bind::gInputSignal], TextureSRV(channel, inTexture));
                binder.Set(table[Bind::gNormalRoughness], guides.normalRoughnessSRV);
                binder.Set(table[Bind::gSceneDepth], guides.sceneDepthSRV);
                binder.Set(table[Bind::gOutputSignal], TextureUAV(channel, outTexture));

                RTDenoiseATrousConstants ac{};
                ac.stepSize = kSteps[pass];
                ac.phiValue = settings.atrousPhiValue;
                ac.phiNormal = settings.atrousPhiNormal;
                ac.phiDepth = settings.atrousPhiDepth;
                ac.traceWidth = static_cast<int>(state.traceWidth);
                ac.traceHeight = static_cast<int>(state.traceHeight);
                ExtractProjectionZW(guides.projection, ac.projM33, ac.projM43);
                ac.traceScale = static_cast<int>(state.traceScale);
                ac.fullWidth = static_cast<int>(state.width);
                ac.fullHeight = static_cast<int>(state.height);
                binder.SetConstants(table[Bind::RTDenoiseATrousConstants], ac);

                binder.ValidateBeforeDraw(table);
                cmdList->Dispatch(groupX, groupY, 1);

                Barrier::UAV(cmdList, outputRes);
                lastTexture = outTexture;
            }
        }

        Resolve(cmdList, channel, lastTexture, guides, settings);

        // 出力はピクセルシェーダから読まれる
        Barrier::Transition(cmdList, TextureResource(channel, Texture::Output),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void RTDenoiser::Resolve(ID3D12GraphicsCommandList* cmdList, uint32_t channel, Texture source,
        const RTDenoiseGuides& guides, const RTDenoiseSettings& settings)
    {
        if (!resolvePass_.initialized) {
            return;
        }

        const Channel& state = channels_[channel];
        GpuResource& sourceRes = TextureResource(channel, source);
        GpuResource& outputRes = TextureResource(channel, Texture::Output);
        if (!sourceRes.IsValid() || !outputRes.IsValid()) {
            return;
        }

        {
            BarrierBatch batch(cmdList);
            batch.Transition(sourceRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Transition(outputRes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        cmdList->SetComputeRootSignature(resolvePass_.GetRootSignature());
        cmdList->SetPipelineState(resolvePass_.pipelineState.Get());

        namespace Bind = RTDenoiseResolveBind;
        const BindingTable& table = resolvePass_.bindings;
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(table[Bind::gTraceSignal], TextureSRV(channel, source));
        binder.Set(table[Bind::gSceneDepth], guides.sceneDepthSRV);
        binder.Set(table[Bind::gNormalRoughness], guides.normalRoughnessSRV);
        binder.Set(table[Bind::gOutputSignal], TextureUAV(channel, Texture::Output));

        RTDenoiseResolveConstants rc{};
        rc.fullWidth = static_cast<int>(state.width);
        rc.fullHeight = static_cast<int>(state.height);
        rc.traceWidth = static_cast<int>(state.traceWidth);
        rc.traceHeight = static_cast<int>(state.traceHeight);
        ExtractProjectionZW(guides.projection, rc.projM33, rc.projM43);
        rc.traceScale = static_cast<int>(state.traceScale);
        rc.phiDepth = settings.upsamplePhiDepth;
        binder.SetConstants(table[Bind::RTDenoiseResolveConstants], rc);

        binder.ValidateBeforeDraw(table);
        cmdList->Dispatch(DispatchGroupCount(state.width), DispatchGroupCount(state.height), 1);

        Barrier::UAV(cmdList, outputRes);
    }
}
