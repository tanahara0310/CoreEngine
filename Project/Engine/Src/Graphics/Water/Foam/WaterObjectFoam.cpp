#include "pch.h"
#include "WaterObjectFoam.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <string>

#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UavTexture.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ShaderProgram.h"
#include "Graphics/Water/WaterSurfaceData.h"
#include "Math/IntegerMath.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        struct WaterObjectFoamConstants {
            float originXZ[2] = { 0.0f, 0.0f };
            float prevOriginXZ[2] = { 0.0f, 0.0f };
            float cellSize = 0.0f;
            float deltaSeconds = 0.0f;
            uint32_t clear = 0;
            uint32_t sourceCount = 0;
            float windDriftVelocity[2] = { 0.0f, 0.0f };
            float flowStrength = 0.0f;
            float decaySeconds = 0.0f;
            float driftOffsetXZ[2] = { 0.0f, 0.0f };
            float bubbleSeconds = 0.0f;
            float riseSeconds = 0.0f;
            float waveGroupPhase[3] = { 0.0f, 0.0f, 0.0f };
            uint32_t useDisplacement = 0;
            int32_t sourceRect[4] = { 0, 0, 0, 0 };
            float shoreMapOriginXZ[2] = { 0.0f, 0.0f };
            float shoreMapTexelsPerMeter = 0.0f;
            float shoreDepthOffset = 0.0f;
            uint32_t shoreMapResolution[2] = { 0, 0 }; ///< 0 = 地図なし
            float shorePad[2] = { 0.0f, 0.0f };
            Vector4 sources[WaterObjectFoam::kMaxSources * 2]{};
        };
        static constexpr Cb::Field kWaterObjectFoamConstantsFields[] = {
            CB_FIELD(WaterObjectFoamConstants, originXZ), CB_FIELD(WaterObjectFoamConstants, prevOriginXZ),
            CB_FIELD(WaterObjectFoamConstants, cellSize), CB_FIELD(WaterObjectFoamConstants, deltaSeconds),
            CB_FIELD(WaterObjectFoamConstants, clear), CB_FIELD(WaterObjectFoamConstants, sourceCount),
            CB_FIELD(WaterObjectFoamConstants, windDriftVelocity), CB_FIELD(WaterObjectFoamConstants, flowStrength),
            CB_FIELD(WaterObjectFoamConstants, decaySeconds), CB_FIELD(WaterObjectFoamConstants, driftOffsetXZ),
            CB_FIELD(WaterObjectFoamConstants, bubbleSeconds), CB_FIELD(WaterObjectFoamConstants, riseSeconds),
            CB_FIELD(WaterObjectFoamConstants, waveGroupPhase), CB_FIELD(WaterObjectFoamConstants, useDisplacement),
            CB_FIELD(WaterObjectFoamConstants, sourceRect), CB_FIELD(WaterObjectFoamConstants, shoreMapOriginXZ),
            CB_FIELD(WaterObjectFoamConstants, shoreMapTexelsPerMeter),
            CB_FIELD(WaterObjectFoamConstants, shoreDepthOffset),
            CB_FIELD(WaterObjectFoamConstants, shoreMapResolution), CB_FIELD(WaterObjectFoamConstants, shorePad),
            CB_FIELD(WaterObjectFoamConstants, sources),
        };
        CB_VERIFY_LAYOUT(WaterObjectFoamConstants, kWaterObjectFoamConstantsFields);
        CB_BIND_HLSL(WaterObjectFoamConstants, kWaterObjectFoamConstantsFields, "WaterObjectFoamConstants");

        namespace AdvanceBind {
            enum Slot : size_t {
                WaterObjectFoamConstants, gFoamPrev, gFlowPrev, gDisplacement, gShoreMap, gFoamOutput, gFlowOutput, Count
            };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "WaterObjectFoamConstants", ShaderBindingType::CBV, BindingUsage::Required },
                { "gFoamPrev",                ShaderBindingType::SRV, BindingUsage::Required },
                { "gFlowPrev",                ShaderBindingType::SRV, BindingUsage::Required },
                { "gDisplacement",            ShaderBindingType::SRV, BindingUsage::Optional },
                { "gShoreMap",                ShaderBindingType::SRV, BindingUsage::Optional },
                { "gFoamOutput",              ShaderBindingType::UAV, BindingUsage::Required },
                { "gFlowOutput",              ShaderBindingType::UAV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }

        constexpr DXGI_FORMAT kFoamFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        constexpr DXGI_FORMAT kFlowFormat = DXGI_FORMAT_R16G16_FLOAT;
        constexpr uint32_t kGroupSize = 8;
        constexpr D3D12_RESOURCE_STATES kAnyShaderResource = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        // 源の幅に対する、源を足す範囲の比（WaterObjectFoamAdvance.CS.hlsl の kSourceReachSigmas と同じ値）
        constexpr float kSourceReachSigmas = 3.0f;
        // 源の幅の下限（1 マスの幅に対する比。シェーダーの kMinSigmaCells と同じ値）
        constexpr float kMinSigmaCells = 0.75f;
        // 源を足す範囲に足す、FFT の波の水平の変位の余裕 [m]
        constexpr float kDisplacementMargin = 8.0f;
        // 泡が無いとみなす量 0.002 まで 1 が減る寿命の倍数 ln(1 / 0.002)
        constexpr float kEmptyFoamLifetimes = 6.2f;
        // 泡の寿命のばらつきのいちばん長い倍率（WaterWhitecapCoverage.hlsli の kWhitecapLifetimeScaleMax と同じ値）
        constexpr float kMaxLifetimeScale = 1.55f;
        constexpr float kMinIdleSeconds = 5.0f;
        constexpr float kMaxIdleSeconds = 60.0f;

        int32_t FloorToCell(float meters)
        {
            return static_cast<int32_t>(std::floor(meters / WaterObjectFoam::kCellSize));
        }
    }

    bool WaterObjectFoam::Initialize(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        ShaderProgramCache* shaderProgramCache)
    {
        dxCommon_ = dxCommon;
        descriptorAllocator_ = descriptorAllocator;
        shaderProgramCache_ = shaderProgramCache;
        if (!dxCommon_ || !descriptorAllocator_ || !shaderProgramCache_) {
            return false;
        }
        sources_.reserve(kMaxSources * 2);
        isInitialized_ = CreatePipeline() && CreateTextures();
        return isInitialized_;
    }

    bool WaterObjectFoam::CreatePipeline()
    {
        Logger& log = Logger::GetInstance();
        const ShaderProgram* program = shaderProgramCache_->GetOrCreateCompute(
            L"Engine/Assets/Shaders/Water/Simulation/WaterObjectFoamAdvance.CS.hlsl", "WaterObjectFoamAdvance");
        if (!program) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterObjectFoam: 物の泡のシェーダーをコンパイルできませんでした");
            return false;
        }

        RootSignatureConfig config;
        config.ConfigureSampler("gLinearClamp", SamplerConfig::LinearClamp());
        config.ConfigureSampler("gLinearWrap", SamplerConfig::Linear());
        const auto buildResult = rootSignature_.Build(dxCommon_->GetDevice(), program->GetReflection(), config);
        if (!buildResult.success) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterObjectFoam: ルートシグネチャを作れませんでした: {}", buildResult.errorMessage);
            return false;
        }

        try {
            bindings_ = BindingTable::Resolve(program->GetReflection(), AdvanceBind::kDecls, "WaterObjectFoamAdvance");
        }
        catch (const std::exception& e) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterObjectFoam: シェーダーの結線が宣言と合いません: {}", e.what());
            return false;
        }

        pipeline_ = ComputePipelineUtil::Create(
            dxCommon_->GetDevice(), rootSignature_.GetRootSignature(), program->GetCS(), "WaterObjectFoamAdvance");
        return pipeline_ != nullptr;
    }

    bool WaterObjectFoam::CreateTextures()
    {
        const Microsoft::WRL::ComPtr<ID3D12Device> device = dxCommon_->GetDevice();
        const D3D12_RESOURCE_DESC foamDesc = UavTexture::MakeDesc(kResolution, kResolution, 1, kFoamFormat);
        const D3D12_RESOURCE_DESC flowDesc = UavTexture::MakeDesc(kResolution, kResolution, 1, kFlowFormat);
        for (uint32_t i = 0; i < 2; ++i) {
            const std::string index = std::to_string(i);
            if (!UavTexture::CreateResource(device, foam_[i], foamDesc, kAnyShaderResource)
                || !UavTexture::CreateResource(device, flow_[i], flowDesc, kAnyShaderResource)) {
                return false;
            }
            UavTexture::EnsureViews(*descriptorAllocator_, foam_[i].Get(), foamDesc,
                foamSrv_[i], "WaterObjectFoamSRV_" + index, foamUav_[i], "WaterObjectFoamUAV_" + index);
            UavTexture::EnsureViews(*descriptorAllocator_, flow_[i].Get(), flowDesc,
                flowSrv_[i], "WaterObjectFoamFlowSRV_" + index, flowUav_[i], "WaterObjectFoamFlowUAV_" + index);
            if (!foamSrv_[i].IsValid() || !foamUav_[i].IsValid() || !flowSrv_[i].IsValid() || !flowUav_[i].IsValid()) {
                return false;
            }
        }
        return true;
    }

    float WaterObjectFoam::ComputeIdleLimit() const
    {
        const float lifetime = kMaxLifetimeScale * (std::max)(settings_.decaySeconds, 0.0f)
            + (std::max)(settings_.bubbleSeconds, 0.0f) + (std::max)(settings_.riseSeconds, 0.0f);
        return std::clamp(kEmptyFoamLifetimes * lifetime, kMinIdleSeconds, kMaxIdleSeconds);
    }

    void WaterObjectFoam::Advance(
        std::span<const Vector4> sources, float deltaSeconds,
        float cameraX, float cameraZ, float viewX, float viewZ, const Settings& settings)
    {
        plannedSteps_ = 0;
        plannedStepSeconds_ = 0.0f;
        plannedClear_ = false;
        sources_.clear();
        if (!isInitialized_) {
            return;
        }
        settings_ = settings;

        const float forward = kForwardShare * static_cast<float>(kResolution) * kCellSize;
        const int32_t snappedX = FloorToCell(cameraX + viewX * forward) - static_cast<int32_t>(kResolution / 2);
        const int32_t snappedZ = FloorToCell(cameraZ + viewZ * forward) - static_cast<int32_t>(kResolution / 2);
        if (!settings.enabled) {
            active_ = false;
            return;
        }

        // 源は 2 つで 1 組。数え方を変えずに、使えない組を捨てる
        const size_t pairCount = (std::min)(sources.size() / 2, static_cast<size_t>(kMaxSources));
        for (size_t i = 0; i < pairCount; ++i) {
            const Vector4& shape = sources[i * 2];
            const Vector4& motion = sources[i * 2 + 1];
            if (!(shape.z > 0.0f) || !std::isfinite(shape.x) || !std::isfinite(shape.y) || !std::isfinite(shape.w)
                || !std::isfinite(motion.x) || !std::isfinite(motion.y) || !std::isfinite(motion.z)) {
                continue;
            }
            sources_.push_back(shape);
            sources_.push_back(motion);
        }

        const float elapsed = (std::max)(deltaSeconds, 0.0f);
        if (!active_) {
            if (sources_.empty() || !(elapsed > 0.0f)) {
                sources_.clear();
                return;
            }
            // 源の来たフレームに、今の窓の位置で 0 から始める
            active_ = true;
            plannedClear_ = true;
            hasWritten_ = false;
            idleSeconds_ = 0.0f;
            originCell_[0] = targetOriginCell_[0] = snappedX;
            originCell_[1] = targetOriginCell_[1] = snappedZ;
            driftOffset_[0] = driftOffset_[1] = 0.0;
        }

        if (!sources_.empty()) {
            idleSeconds_ = 0.0f;
        }
        idleSeconds_ += elapsed;
        if (idleSeconds_ > ComputeIdleLimit()) {
            active_ = false;
            sources_.clear();
            return;
        }
        if (!(elapsed > 0.0f)) {
            return;
        }

        // 長いフレームは 2 歩に割り、それより長い分は捨てる
        const float seconds = (std::min)(elapsed, static_cast<float>(kMaxStepsPerFrame) * kMaxStepSeconds);
        plannedSteps_ = std::clamp(
            static_cast<uint32_t>(std::ceil(seconds / kMaxStepSeconds - 1.0e-3f)), 1u, kMaxStepsPerFrame);
        plannedStepSeconds_ = seconds / static_cast<float>(plannedSteps_);
        targetOriginCell_[0] = snappedX;
        targetOriginCell_[1] = snappedZ;
    }

    void WaterObjectFoam::Dispatch(
        ID3D12GraphicsCommandList* cmdList, uint64_t frameNumber, const WaterFFTOceanInput* fftOcean)
    {
        if (!isInitialized_ || !cmdList || frameNumber == lastFrameNumber_) {
            return;
        }
        lastFrameNumber_ = frameNumber;
        if (active_) {
            for (uint32_t step = 0; step < plannedSteps_; ++step) {
                Step(cmdList, plannedStepSeconds_, plannedClear_ && step == 0, fftOcean);
            }
        }
        plannedSteps_ = 0;
        plannedClear_ = false;
        sources_.clear();
    }

    std::array<int32_t, 4> WaterObjectFoam::ComputeSourceRect(float margin) const
    {
        const int32_t resolution = static_cast<int32_t>(kResolution);
        std::array<int32_t, 4> rect = { resolution, resolution, 0, 0 };
        const float originX = static_cast<float>(targetOriginCell_[0]) * kCellSize;
        const float originZ = static_cast<float>(targetOriginCell_[1]) * kCellSize;
        for (size_t i = 0; i + 1 < sources_.size(); i += 2) {
            const Vector4& shape = sources_[i];
            const float reach = kSourceReachSigmas * (std::max)(shape.z, kMinSigmaCells * kCellSize) + margin;
            const int32_t minX = (std::max)(FloorToCell(shape.x - reach - originX), 0);
            const int32_t minZ = (std::max)(FloorToCell(shape.y - reach - originZ), 0);
            const int32_t maxX = (std::min)(FloorToCell(shape.x + reach - originX) + 1, resolution);
            const int32_t maxZ = (std::min)(FloorToCell(shape.y + reach - originZ) + 1, resolution);
            if (minX >= maxX || minZ >= maxZ) {
                continue;
            }
            rect[0] = (std::min)(rect[0], minX);
            rect[1] = (std::min)(rect[1], minZ);
            rect[2] = (std::max)(rect[2], maxX);
            rect[3] = (std::max)(rect[3], maxZ);
        }
        return rect;
    }

    void WaterObjectFoam::Step(
        ID3D12GraphicsCommandList* cmdList, float stepSeconds, bool clear, const WaterFFTOceanInput* fftOcean)
    {
        const bool useDisplacement = fftOcean && fftOcean->enabled != 0 && fftOcean->displacementSRV.ptr != 0;
        driftOffset_[0] += static_cast<double>(settings_.windDriftVelocity[0]) * stepSeconds;
        driftOffset_[1] += static_cast<double>(settings_.windDriftVelocity[1]) * stepSeconds;
        flowTime_ = std::fmod(flowTime_ + stepSeconds, kFlowTimePeriodSeconds);

        WaterObjectFoamConstants constants{};
        for (int c = 0; c < 2; ++c) {
            constants.originXZ[c] = static_cast<float>(targetOriginCell_[c]) * kCellSize;
            constants.prevOriginXZ[c] = static_cast<float>(originCell_[c]) * kCellSize;
            constants.windDriftVelocity[c] = settings_.windDriftVelocity[c];
            constants.driftOffsetXZ[c] = static_cast<float>(driftOffset_[c]);
        }
        constants.cellSize = kCellSize;
        constants.deltaSeconds = stepSeconds;
        constants.clear = clear ? 1u : 0u;
        constants.flowStrength = (std::max)(settings_.flowStrength, 0.0f);
        constants.decaySeconds = settings_.decaySeconds;
        constants.bubbleSeconds = settings_.bubbleSeconds;
        constants.riseSeconds = (std::max)(settings_.riseSeconds, 0.0f);
        constants.useDisplacement = useDisplacement ? 1u : 0u;
        if (useDisplacement) {
            for (int c = 0; c < 3; ++c) {
                constants.waveGroupPhase[c] = fftOcean->waveGroupPhase[c];
            }
        }

        constants.sourceCount = static_cast<uint32_t>(sources_.size() / 2);
        std::copy(sources_.begin(), sources_.end(), constants.sources);
        const std::array<int32_t, 4> rect = ComputeSourceRect(useDisplacement ? kDisplacementMargin : 0.0f);
        std::copy(rect.begin(), rect.end(), constants.sourceRect);
        if (rect[2] <= rect[0] || rect[3] <= rect[1]) {
            constants.sourceCount = 0;
        }

        if (shoreMap_.has_value()) {
            constants.shoreMapOriginXZ[0] = shoreMap_->originXZ[0];
            constants.shoreMapOriginXZ[1] = shoreMap_->originXZ[1];
            constants.shoreMapTexelsPerMeter = shoreMap_->texelsPerMeter;
            constants.shoreDepthOffset = shoreMap_->depthOffset;
            constants.shoreMapResolution[0] = shoreMap_->resolution[0];
            constants.shoreMapResolution[1] = shoreMap_->resolution[1];
        }

        const uint32_t read = current_;
        const uint32_t write = 1u - current_;
        {
            BarrierBatch barriers(cmdList);
            barriers.Transition(foam_[write], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            barriers.Transition(flow_[write], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            barriers.Transition(foam_[read], kAnyShaderResource);
            barriers.Transition(flow_[read], kAnyShaderResource);
            barriers.Flush();
        }

        const D3D12_GPU_VIRTUAL_ADDRESS address = dxCommon_->GetUploadRing().AllocateConstants(constants);
        cmdList->SetPipelineState(pipeline_.Get());
        cmdList->SetComputeRootSignature(rootSignature_.GetRootSignature());
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(bindings_[AdvanceBind::WaterObjectFoamConstants], address);
        binder.Set(bindings_[AdvanceBind::gFoamPrev], foamSrv_[read].gpuHandle);
        binder.Set(bindings_[AdvanceBind::gFlowPrev], flowSrv_[read].gpuHandle);
        if (useDisplacement) {
            binder.Set(bindings_[AdvanceBind::gDisplacement], fftOcean->displacementSRV);
        }
        if (shoreMap_.has_value()) {
            binder.Set(bindings_[AdvanceBind::gShoreMap], shoreMap_->srv);
        }
        binder.Set(bindings_[AdvanceBind::gFoamOutput], foamUav_[write].gpuHandle);
        binder.Set(bindings_[AdvanceBind::gFlowOutput], flowUav_[write].gpuHandle);
        binder.ValidateBeforeDraw(bindings_);
        const UINT groups = DivRoundUp(kResolution, kGroupSize);
        cmdList->Dispatch(groups, groups, 1);

        {
            BarrierBatch barriers(cmdList);
            barriers.Transition(foam_[write], kAnyShaderResource);
            barriers.Transition(flow_[write], kAnyShaderResource);
            barriers.Flush();
        }

        current_ = write;
        originCell_[0] = targetOriginCell_[0];
        originCell_[1] = targetOriginCell_[1];
        hasWritten_ = true;
    }

    void WaterObjectFoam::FillBinding(WaterObjectFoamBinding& out) const
    {
        out = {};
        if (!isInitialized_ || !active_ || (!hasWritten_ && plannedSteps_ == 0)) {
            return;
        }
        // このフレームの Dispatch が最後に書く組と、その後の範囲
        const uint32_t finalSide = (plannedSteps_ % 2u == 1u) ? 1u - current_ : current_;
        const bool willStep = plannedSteps_ > 0;
        const float stepSeconds = static_cast<float>(plannedSteps_) * plannedStepSeconds_;
        out.enabled = 1;
        out.foamSRV = foamSrv_[finalSide].gpuHandle;
        out.flowSRV = flowSrv_[finalSide].gpuHandle;
        out.invSize = 1.0f / (kCellSize * static_cast<float>(kResolution));
        for (int c = 0; c < 2; ++c) {
            const int32_t originCell = willStep ? targetOriginCell_[c] : originCell_[c];
            out.originXZ[c] = static_cast<float>(originCell) * kCellSize;
            out.driftOffsetXZ[c] = static_cast<float>(driftOffset_[c] + settings_.windDriftVelocity[c] * stepSeconds);
        }
        out.flowTime = static_cast<float>(std::fmod(flowTime_ + stepSeconds, kFlowTimePeriodSeconds));
    }
}
