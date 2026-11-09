#include "pch.h"
#include "RTAmbientOcclusion.h"

#include <algorithm>
#include <exception>
#include <iterator>

#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ShaderProgram.h"
#include "Utility/CVar/CVar.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        CVar<bool> cvEnabled{
            "r.RTAO.Enabled", true,
            "レイトレーシングが使えるとき、SSAO の代わりに RTAO で環境光を遮る。"
            "AO そのものの ON/OFF は SSAO の切り替え（r.SSAO.Enabled）に従う" };

        CVar<float> cvRadius{
            "r.RTAO.Radius", 1.0f,
            "レイの長さ [m]。これより遠い物は遮らない。長いほど広い範囲が暗くなり、重くなる",
            CVarRange{ 0.1f, 5.0f } };

        CVar<float> cvIntensity{
            "r.RTAO.Intensity", 1.0f,
            "遮りの強さ。0 で遮らない・1 でレイの結果のまま",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvBias{
            "r.RTAO.Bias", 0.02f,
            "レイの始点を面から離す距離 [m]。小さいほど物の接した所まで暗くなるが、面が自分を遮ってざらつく",
            CVarRange{ 0.0f, 0.2f } };

        CVar<int> cvSampleCount{
            "r.RTAO.SampleCount", 1,
            "1 画素に撃つレイの本数。重さはほぼ本数に比例する",
            CVarRange{ 1.0f, 16.0f } };

        CVar<int> cvNewPixelSampleCount{
            "r.RTAO.NewPixelSampleCount", 4,
            "現れた直後の画素（前のフレームと重ねた数が少ない所）に撃つレイの本数",
            CVarRange{ 1.0f, 16.0f } };

        CVar<bool> cvHalfResolutionTrace{
            "r.RTAO.HalfResolutionTrace", true,
            "1/2 解像度で撃ってノイズを取り、仕上げでフル解像度へ戻す。レイの本数が 1/4 になる" };

        CVar<int> cvMaxHistoryFrames{
            "r.RTAO.MaxHistoryFrames", 32,
            "時間方向に重ねるフレーム数の上限。大きいほどなめらかだが、物が動いたときの追従が遅れる",
            CVarRange{ 1.0f, 255.0f } };

        CVar<int> cvAtrousPassCount{
            "r.RTAO.AtrousPassCount", 3,
            "A-Trous のパス数。0 で空間のフィルタを掛けない",
            CVarRange{ 0.0f, 4.0f } };

        CVar<bool> cvHistoryEnabled{
            "r.RTAO.HistoryEnabled", true,
            "前のフレームの履歴を読む（切り分け用）",
            CVarRange{}, CVarFlags::NoSave };

        /// @brief 信号の出どころを見分ける値（RTAO の信号は 1 つだけ）
        constexpr uint64_t kSignalKey = 1;

        /// @brief 1 画素に撃つレイの本数の上限（RTAO.CS.hlsl の kMaxSamples と同じ）
        constexpr int kMaxSamples = 16;

        /// @brief 8x8 のスレッドグループで要るグループの数
        UINT DispatchGroupCount(UINT pixels) { return (pixels + 7) / 8; }

        /// @brief RTAO.CS.hlsl の RTAOConstants
        struct alignas(16) RTAOConstants {
            Matrix4x4 invViewProj;          // offset   0
            uint32_t fullWidth;             // offset  64
            uint32_t fullHeight;            // offset  68
            uint32_t traceWidth;            // offset  72
            uint32_t traceHeight;           // offset  76
            uint32_t traceScale;            // offset  80
            uint32_t frameIndex;            // offset  84
            uint32_t sampleCount;           // offset  88
            uint32_t newPixelSampleCount;   // offset  92
            float radius;                   // offset  96
            float intensity;                // offset 100
            float bias;                     // offset 104
            uint32_t historyValid;          // offset 108
            uint32_t hitInstanceTableIndex; // offset 112
            uint32_t hitSubMeshTableIndex;  // offset 116
            uint32_t pad0;                  // offset 120
            uint32_t pad1;                  // offset 124
        };
        static_assert(sizeof(RTAOConstants) == 128, "RTAOConstants と HLSL の cbuffer の大きさが違う");

        constexpr Cb::Field kRTAOConstantsFields[] = {
            CB_FIELD(RTAOConstants, invViewProj),
            CB_FIELD(RTAOConstants, fullWidth), CB_FIELD(RTAOConstants, fullHeight),
            CB_FIELD(RTAOConstants, traceWidth), CB_FIELD(RTAOConstants, traceHeight),
            CB_FIELD(RTAOConstants, traceScale), CB_FIELD(RTAOConstants, frameIndex),
            CB_FIELD(RTAOConstants, sampleCount), CB_FIELD(RTAOConstants, newPixelSampleCount),
            CB_FIELD(RTAOConstants, radius), CB_FIELD(RTAOConstants, intensity),
            CB_FIELD(RTAOConstants, bias), CB_FIELD(RTAOConstants, historyValid),
            CB_FIELD(RTAOConstants, hitInstanceTableIndex), CB_FIELD(RTAOConstants, hitSubMeshTableIndex),
            CB_FIELD(RTAOConstants, pad0), CB_FIELD(RTAOConstants, pad1),
        };
        CB_VERIFY_LAYOUT(RTAOConstants, kRTAOConstantsFields);
        CB_BIND_HLSL(RTAOConstants, kRTAOConstantsFields, "RTAOConstants");

        namespace RTAOBind {
            enum Slot : size_t { RTAOConstants, gScene, gSceneDepth, gNormalRoughness, gAOHistory, gAOOutput, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "RTAOConstants",    ShaderBindingType::CBV, BindingUsage::Required },
                { "gScene",           ShaderBindingType::SRV, BindingUsage::Required },
                { "gSceneDepth",      ShaderBindingType::SRV, BindingUsage::Required },
                { "gNormalRoughness", ShaderBindingType::SRV, BindingUsage::Required },
                { "gAOHistory",       ShaderBindingType::SRV, BindingUsage::Required },
                { "gAOOutput",        ShaderBindingType::UAV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }
    }

    bool RTAmbientOcclusion::IsEnabledByCVar()
    {
        return cvEnabled.Get();
    }

    bool RTAmbientOcclusion::Initialize(const RTEffectInitContext& init)
    {
        Logger& log = Logger::GetInstance();
        dxCommon_ = init.dxCommon;
        asMgr_ = init.asMgr;
        if (!dxCommon_ || !asMgr_ || !init.shaderProgramCache) {
            return false;
        }

        const ShaderProgram* program = init.shaderProgramCache->GetOrCreateCompute(L"RTAO.CS.hlsl", "RTAO", L"cs_6_6");
        if (!program) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline, "RTAO: シェーダーをコンパイルできませんでした");
            return false;
        }

        // 物の材質の表とテクスチャをヒープ内の番号で引く（Include/RayTracing/RTSceneTables.hlsli）
        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
        config.ConfigureSampler("gLinearWrap", SamplerConfig::Linear());
        const auto buildResult = rootSigMgr_.Build(dxCommon_->GetDevice(), program->GetReflection(), config);
        if (!buildResult.success) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "RTAO: ルートシグネチャを作れませんでした: {}", buildResult.errorMessage);
            return false;
        }

        try {
            bindings_ = BindingTable::Resolve(program->GetReflection(), RTAOBind::kDecls, RTAOBind::Count, "RTAO");
        }
        catch (const std::exception& e) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "RTAO: シェーダーの結線が宣言と合いません: {}", e.what());
            return false;
        }

        pipelineState_ = ComputePipelineUtil::Create(
            dxCommon_->GetDevice(), rootSigMgr_.GetRootSignature(), program->GetCS(), "RTAO");
        if (!pipelineState_) {
            return false;
        }

        initialized_ = denoiser_.Initialize(init, "RTAO", 1);
        return initialized_;
    }

    void RTAmbientOcclusion::BeginFrame()
    {
        dispatchedThisFrame_ = false;
    }

    void RTAmbientOcclusion::OnResize(uint32_t width, uint32_t height)
    {
        if (initialized_) {
            denoiser_.OnResize(width, height, GetTraceScale());
        }
    }

    void RTAmbientOcclusion::SyncSettingsFromCVars()
    {
        settings_.radius = cvRadius.Get();
        settings_.intensity = cvIntensity.Get();
        settings_.bias = cvBias.Get();
        settings_.sampleCount = cvSampleCount.Get();
        settings_.newPixelSampleCount = cvNewPixelSampleCount.Get();
        settings_.halfResolutionTrace = cvHalfResolutionTrace.Get();
        settings_.maxHistoryFrames = cvMaxHistoryFrames.Get();
        settings_.atrousPassCount = cvAtrousPassCount.Get();
        settings_.useHistory = cvHistoryEnabled.Get();
    }

    RTDenoiseSettings RTAmbientOcclusion::MakeDenoiseSettings() const
    {
        RTDenoiseSettings denoise{};
        denoise.maxHistoryFrames = settings_.maxHistoryFrames;
        denoise.atrousPassCount = settings_.atrousPassCount;
        denoise.useHistory = settings_.useHistory;
        return denoise;
    }

    void RTAmbientOcclusion::Trace(ID3D12GraphicsCommandList* cmdList,
        const RTDenoiseGuides& guides,
        const Matrix4x4& invViewProj,
        UINT width, UINT height,
        const RTHitTableIndices& hitTables)
    {
        SyncSettingsFromCVars();

        const UINT traceScale = GetTraceScale();
        const UINT traceWidth = (width + traceScale - 1) / traceScale;
        const UINT traceHeight = (height + traceScale - 1) / traceScale;
        const int sampleCount = std::clamp(settings_.sampleCount, 1, kMaxSamples);
        const int newPixelSampleCount = std::clamp(settings_.newPixelSampleCount, 1, kMaxSamples);

        // 失敗した経路でも状態がデバッグ窓に出るように、先に埋める
        dispatchInfo_ = {};
        dispatchInfo_.passName = "RTAO";
        dispatchInfo_.width = traceWidth;
        dispatchInfo_.height = traceHeight;
        dispatchInfo_.blasCount = asMgr_ ? asMgr_->GetBLASCount() : 0;
        dispatchInfo_.AddExtra("traceScale", static_cast<float>(traceScale));
        dispatchInfo_.AddExtra("samples/px", static_cast<float>(sampleCount));
        dispatchInfo_.AddExtra("newPixelSpp", static_cast<float>(newPixelSampleCount));
        dispatchInfo_.AddExtra("radius[m]", settings_.radius);
        dispatchInfo_.AddExtra("atrousPasses", static_cast<float>(RTDenoiser::ClampAtrousPassCount(settings_.atrousPassCount)));

        if (!initialized_) {
            dispatchInfo_.status = RayTracingDispatchStatus::NotInitialized;
            return;
        }
        if (!asMgr_ || !asMgr_->IsSupported()) {
            dispatchInfo_.status = RayTracingDispatchStatus::RayTracingUnsupported;
            return;
        }
        if (!asMgr_->IsTLASReady()) {
            dispatchInfo_.status = RayTracingDispatchStatus::NoTLAS;
            return;
        }
        if (!cmdList) {
            dispatchInfo_.status = RayTracingDispatchStatus::InvalidCommandList;
            return;
        }
        if (guides.sceneDepthSRV.ptr == 0 || guides.normalRoughnessSRV.ptr == 0 || width == 0 || height == 0) {
            dispatchInfo_.status = RayTracingDispatchStatus::Skipped;
            return;
        }

        if (!denoiser_.Prepare(kDenoiseChannel, width, height, traceScale, kSignalKey)) {
            dispatchInfo_.status = RayTracingDispatchStatus::OutputAllocationFailed;
            return;
        }
        dispatchInfo_.outputSrvPtr = GetOutputSRV().ptr;

        RTAOConstants constants{};
        constants.invViewProj = invViewProj;
        constants.fullWidth = width;
        constants.fullHeight = height;
        constants.traceWidth = traceWidth;
        constants.traceHeight = traceHeight;
        constants.traceScale = traceScale;
        constants.frameIndex = RTDebug::NoiseFrame(frameCount_++);
        constants.sampleCount = static_cast<uint32_t>(sampleCount);
        constants.newPixelSampleCount = static_cast<uint32_t>(newPixelSampleCount);
        constants.radius = settings_.radius;
        constants.intensity = settings_.intensity;
        constants.bias = settings_.bias;
        constants.historyValid = (denoiser_.IsHistoryValid(kDenoiseChannel) && settings_.useHistory) ? 1u : 0u;
        // 表がそろわないフレームはアルファを調べず、どの物も不透明として当てる
        constants.hitInstanceTableIndex = hitTables.IsValid() ? hitTables.instanceTable : kRTHitNoTexture;
        constants.hitSubMeshTableIndex = hitTables.IsValid() ? hitTables.subMeshTable : kRTHitNoTexture;

        cmdList->SetComputeRootSignature(rootSigMgr_.GetRootSignature());
        cmdList->SetPipelineState(pipelineState_.Get());

        // 入力へ書き、前フレームの履歴（現れた直後の画素の判定）を読む
        denoiser_.BeginInputWrite(cmdList, kDenoiseChannel);

        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(bindings_[RTAOBind::RTAOConstants], dxCommon_->GetUploadRing().AllocateConstants(constants));
        binder.Set(bindings_[RTAOBind::gScene], asMgr_->GetTLASSRVHandle());
        binder.Set(bindings_[RTAOBind::gSceneDepth], guides.sceneDepthSRV);
        binder.Set(bindings_[RTAOBind::gNormalRoughness], guides.normalRoughnessSRV);
        binder.Set(bindings_[RTAOBind::gAOHistory], denoiser_.GetPreviousHistorySRV(kDenoiseChannel));
        binder.Set(bindings_[RTAOBind::gAOOutput], denoiser_.GetInputUAV(kDenoiseChannel));
        binder.ValidateBeforeDraw(bindings_);
        cmdList->Dispatch(DispatchGroupCount(traceWidth), DispatchGroupCount(traceHeight), 1);

        // 時間の蓄積が読む前に書き込みの完了を待つ
        denoiser_.EndInputWrite(cmdList, kDenoiseChannel);

        dispatchedThisFrame_ = true;
        dispatchInfo_.status = RayTracingDispatchStatus::Dispatched;
        dispatchInfo_.rayCount =
            static_cast<uint64_t>(traceWidth) * static_cast<uint64_t>(traceHeight) * static_cast<uint64_t>(sampleCount);
    }

    void RTAmbientOcclusion::Accumulate(ID3D12GraphicsCommandList* cmdList, const RTDenoiseGuides& guides)
    {
        if (!dispatchedThisFrame_) {
            return;
        }
        denoiser_.Accumulate(cmdList, kDenoiseChannel, guides, MakeDenoiseSettings());
    }

    void RTAmbientOcclusion::Filter(ID3D12GraphicsCommandList* cmdList, const RTDenoiseGuides& guides)
    {
        if (!dispatchedThisFrame_) {
            return;
        }
        denoiser_.Filter(cmdList, kDenoiseChannel, guides, MakeDenoiseSettings());
    }
}
