#include "pch.h"
#include "WaterRippleSimulation.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <span>

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/Render/RenderDomainContext.h"
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
#include "Math/Vector/Vector4.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        struct WaterRippleInjectConstants {
            float originXZ[2] = { 0.0f, 0.0f };
            float cellSize = 0.0f;
            uint32_t level = 0;
            uint32_t pair = 0;
            uint32_t sourceCount = 0;
            float lowerBlur = 0.0f;
            float upperBlur = 0.0f;
            int32_t rectMin[2] = { 0, 0 };
            uint32_t rectSize[2] = { 0, 0 };
            uint32_t objectSourceBegin = 0; ///< これより後ろの源は物が起こした波紋の場へも入れる
            uint32_t padding[3] = { 0, 0, 0 };
            Vector4 sources[WaterRippleSimulation::kMaxSourcesPerFrame]{};
        };
        static constexpr Cb::Field kWaterRippleInjectConstantsFields[] = {
            CB_FIELD(WaterRippleInjectConstants, originXZ), CB_FIELD(WaterRippleInjectConstants, cellSize),
            CB_FIELD(WaterRippleInjectConstants, level), CB_FIELD(WaterRippleInjectConstants, pair),
            CB_FIELD(WaterRippleInjectConstants, sourceCount), CB_FIELD(WaterRippleInjectConstants, lowerBlur),
            CB_FIELD(WaterRippleInjectConstants, upperBlur), CB_FIELD(WaterRippleInjectConstants, rectMin),
            CB_FIELD(WaterRippleInjectConstants, rectSize), CB_FIELD(WaterRippleInjectConstants, objectSourceBegin),
            CB_FIELD(WaterRippleInjectConstants, padding), CB_FIELD(WaterRippleInjectConstants, sources),
        };
        CB_VERIFY_LAYOUT(WaterRippleInjectConstants, kWaterRippleInjectConstantsFields);
        CB_BIND_HLSL(WaterRippleInjectConstants, kWaterRippleInjectConstantsFields, "WaterRippleInjectConstants");

        struct WaterRipplePropagateConstants {
            int32_t shiftCells[2] = { 0, 0 };
            uint32_t level = 0;
            uint32_t readPair = 0;
            float courant = 0.0f;
            float keep = 1.0f;
            float invTwoCellSize = 0.0f;
            uint32_t clear = 0;
        };
        static constexpr Cb::Field kWaterRipplePropagateConstantsFields[] = {
            CB_FIELD(WaterRipplePropagateConstants, shiftCells), CB_FIELD(WaterRipplePropagateConstants, level),
            CB_FIELD(WaterRipplePropagateConstants, readPair), CB_FIELD(WaterRipplePropagateConstants, courant),
            CB_FIELD(WaterRipplePropagateConstants, keep), CB_FIELD(WaterRipplePropagateConstants, invTwoCellSize),
            CB_FIELD(WaterRipplePropagateConstants, clear),
        };
        CB_VERIFY_LAYOUT(WaterRipplePropagateConstants, kWaterRipplePropagateConstantsFields);
        CB_BIND_HLSL(WaterRipplePropagateConstants, kWaterRipplePropagateConstantsFields, "WaterRipplePropagateConstants");

        namespace InjectBind {
            enum Slot : size_t { WaterRippleInjectConstants, gRippleState, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "WaterRippleInjectConstants", ShaderBindingType::CBV, BindingUsage::Required },
                { "gRippleState",               ShaderBindingType::UAV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }

        namespace PropagateBind {
            enum Slot : size_t { WaterRipplePropagateConstants, gRippleState, gRippleSurface, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "WaterRipplePropagateConstants", ShaderBindingType::CBV, BindingUsage::Required },
                { "gRippleState",                  ShaderBindingType::UAV, BindingUsage::Required },
                { "gRippleSurface",                ShaderBindingType::UAV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }

        constexpr DXGI_FORMAT kStateFormat = DXGI_FORMAT_R32_FLOAT;
        constexpr DXGI_FORMAT kSurfaceFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        constexpr uint32_t kStateSliceCount = WaterRippleSimulation::kLevelCount * WaterRippleSimulation::kFieldCount * 4;
        constexpr uint32_t kInjectGroupSize = 8;
        constexpr uint32_t kPropagateGroupSize = 16;
        constexpr float kGravity = 9.81f;
        // 細かい段でつぶれないくぼみの幅の下限（細かい段の 1 マスの幅に対する比）
        constexpr float kMinSigmaCells = 0.7f;
        // 源の影響を計算する範囲（ぼかした後の幅に対する比。シェーダーの打ち切りと同じ）
        constexpr float kSourceReachSigmas = 4.0f;
        // 振幅がこの割合まで減ったら段を止める
        constexpr float kIdleAmplitude = 1.0e-3f;
        constexpr float kMinIdleSeconds = 5.0f;
        constexpr float kMaxIdleSeconds = 60.0f;

        int32_t FloorToCell(float meters, float cellSize)
        {
            return static_cast<int32_t>(std::floor(meters / cellSize));
        }

        /// @brief 段の帯の細かい側のぼかしの幅 [m]（いちばん細かい段は 0）
        float LowerBlur(uint32_t level)
        {
            return (level == 0) ? 0.0f : WaterRippleSimulation::kCellSizes[level];
        }

        /// @brief 段の帯の粗い側のぼかしの幅 [m]（いちばん粗い段は 0 = 引かない）
        float UpperBlur(uint32_t level)
        {
            return (level + 1 < WaterRippleSimulation::kLevelCount) ? WaterRippleSimulation::kCellSizes[level + 1] : 0.0f;
        }

        bool BuildPipeline(
            GraphicsCore* dxCommon,
            ShaderProgramCache* cache,
            const wchar_t* path,
            const char* name,
            std::span<const ShaderBindingDecl> decls,
            RootSignatureManager& rootSignature,
            Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline,
            BindingTable& bindings)
        {
            Logger& log = Logger::GetInstance();
            const ShaderProgram* program = cache->GetOrCreateCompute(path, name);
            if (!program) {
                log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                    "WaterRippleSimulation: {} をコンパイルできませんでした", name);
                return false;
            }

            RootSignatureConfig config;
            const auto buildResult = rootSignature.Build(dxCommon->GetDevice(), program->GetReflection(), config);
            if (!buildResult.success) {
                log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                    "WaterRippleSimulation: {} のルートシグネチャを作れませんでした: {}", name, buildResult.errorMessage);
                return false;
            }

            try {
                bindings = BindingTable::Resolve(program->GetReflection(), decls.data(), decls.size(), name);
            }
            catch (const std::exception& e) {
                log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                    "WaterRippleSimulation: {} の結線が宣言と合いません: {}", name, e.what());
                return false;
            }

            pipeline = ComputePipelineUtil::Create(
                dxCommon->GetDevice(), rootSignature.GetRootSignature(), program->GetCS(), name);
            return pipeline != nullptr;
        }
    }

    bool WaterRippleSimulation::Initialize(
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
        pendingSources_.reserve(kMaxSourcesPerFrame);
        isInitialized_ = CreatePipelines() && CreateTextures();
        return isInitialized_;
    }

    bool WaterRippleSimulation::CreatePipelines()
    {
        return BuildPipeline(dxCommon_, shaderProgramCache_,
                   L"Engine/Assets/Shaders/Water/Simulation/WaterRippleInject.CS.hlsl", "WaterRippleInject",
                   InjectBind::kDecls, injectRootSignature_, injectPipeline_, injectBindings_)
            && BuildPipeline(dxCommon_, shaderProgramCache_,
                   L"Engine/Assets/Shaders/Water/Simulation/WaterRipplePropagate.CS.hlsl", "WaterRipplePropagate",
                   PropagateBind::kDecls, propagateRootSignature_, propagatePipeline_, propagateBindings_);
    }

    bool WaterRippleSimulation::CreateTextures()
    {
        const Microsoft::WRL::ComPtr<ID3D12Device> device = dxCommon_->GetDevice();

        D3D12_RESOURCE_DESC stateDesc = UavTexture::MakeDesc(kResolution, kResolution, 1, kStateFormat);
        stateDesc.DepthOrArraySize = static_cast<UINT16>(kStateSliceCount);
        if (!UavTexture::CreateResource(device, state_, stateDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) {
            return false;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC stateUavDesc{};
        stateUavDesc.Format = kStateFormat;
        stateUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        stateUavDesc.Texture2DArray.ArraySize = kStateSliceCount;
        stateUav_ = descriptorAllocator_->CreateUAV(state_.Get(), stateUavDesc, "WaterRippleStateUAV");

        D3D12_RESOURCE_DESC surfaceDesc = UavTexture::MakeDesc(kResolution, kResolution, 1, kSurfaceFormat);
        surfaceDesc.DepthOrArraySize = static_cast<UINT16>(kLevelCount);
        if (!UavTexture::CreateResource(device, surface_, surfaceDesc, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE)) {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC surfaceSrvDesc{};
        surfaceSrvDesc.Format = kSurfaceFormat;
        surfaceSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        surfaceSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        surfaceSrvDesc.Texture2DArray.MipLevels = 1;
        surfaceSrvDesc.Texture2DArray.ArraySize = kLevelCount;
        surfaceSrv_ = descriptorAllocator_->CreateSRV(surface_.Get(), surfaceSrvDesc, "WaterRippleSurfaceSRV");

        D3D12_UNORDERED_ACCESS_VIEW_DESC surfaceUavDesc{};
        surfaceUavDesc.Format = kSurfaceFormat;
        surfaceUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        surfaceUavDesc.Texture2DArray.ArraySize = kLevelCount;
        surfaceUav_ = descriptorAllocator_->CreateUAV(surface_.Get(), surfaceUavDesc, "WaterRippleSurfaceUAV");

        return stateUav_.IsValid() && surfaceSrv_.IsValid() && surfaceUav_.IsValid();
    }

    void WaterRippleSimulation::AddRipple(const Vector3& position, float radius, float depth)
    {
        AddSource(position, radius, depth, false);
    }

    void WaterRippleSimulation::AddObjectRipple(const Vector3& position, float radius, float depth)
    {
        AddSource(position, radius, depth, true);
    }

    void WaterRippleSimulation::AddSource(const Vector3& position, float radius, float depth, bool fromObject)
    {
        if (!isInitialized_ || !(radius > 0.0f) || depth == 0.0f || !std::isfinite(depth)
            || !std::isfinite(position.x) || !std::isfinite(position.z)) {
            return;
        }
        if (pendingSources_.size() >= kMaxSourcesPerFrame) {
            return;
        }

        // 細かい段で表せない細いくぼみは、体積を保ったまま広げる
        const float requestedSigma = 0.5f * radius;
        const float sigma = (std::max)(requestedSigma, kMinSigmaCells * kCellSizes[0]);
        const float volumeScale = (requestedSigma * requestedSigma) / (sigma * sigma);
        pendingSources_.push_back(Source{ position.x, position.z, sigma, depth * volumeScale, fromObject });
    }

    void WaterRippleSimulation::Advance(
        float deltaSeconds, float cameraX, float cameraZ, float viewX, float viewZ, const Settings& settings)
    {
        if (!isInitialized_) {
            return;
        }
        if (!settings.enabled) {
            pendingSources_.clear();
            for (Level& level : levels_) {
                level.active = false;
            }
            return;
        }

        dampingPerSecond_ = (std::max)(settings.dampingPerSecond, 0.0f);
        const float idleLimit = (dampingPerSecond_ > 0.0f)
            ? std::clamp(-std::log(kIdleAmplitude) / dampingPerSecond_, kMinIdleSeconds, kMaxIdleSeconds)
            : kMaxIdleSeconds;
        const float elapsed = (std::max)(deltaSeconds, 0.0f);
        const bool hasSources = !pendingSources_.empty();

        for (uint32_t index = 0; index < kLevelCount; ++index) {
            Level& level = levels_[index];
            const float cellSize = kCellSizes[index];
            const float forward = kForwardShare * static_cast<float>(kResolution) * cellSize;
            const float centerX = cameraX + viewX * forward;
            const float centerZ = cameraZ + viewZ * forward;
            const int32_t snappedX = FloorToCell(centerX, cellSize) - static_cast<int32_t>(kResolution / 2);
            const int32_t snappedZ = FloorToCell(centerZ, cellSize) - static_cast<int32_t>(kResolution / 2);

            if (!level.active) {
                // 起こすときの窓の位置として控える
                level.targetOriginCell[0] = snappedX;
                level.targetOriginCell[1] = snappedZ;
                continue;
            }
            if (hasSources) {
                level.idleSeconds = 0.0f;
            }

            level.idleSeconds += elapsed;
            if (level.idleSeconds > idleLimit) {
                level.active = false;
                continue;
            }

            level.accumulatedSeconds += elapsed;
            const float stepSeconds = kStepSeconds[index];
            const uint32_t newSteps = static_cast<uint32_t>(level.accumulatedSeconds / stepSeconds);
            level.accumulatedSeconds -= static_cast<float>(newSteps) * stepSeconds;
            level.pendingSteps = (std::min)(level.pendingSteps + newSteps, kMaxStepsPerFrame);
            if (level.pendingSteps > 0) {
                level.targetOriginCell[0] = snappedX;
                level.targetOriginCell[1] = snappedZ;
            }
        }
    }

    void WaterRippleSimulation::Dispatch(ID3D12GraphicsCommandList* cmdList, uint64_t frameNumber)
    {
        if (!isInitialized_ || !cmdList || frameNumber == lastFrameNumber_) {
            return;
        }
        lastFrameNumber_ = frameNumber;

        // 源があれば止まっている段を控えた窓の位置で起こし、どの段も消えるまでの時間を数え直す
        if (!pendingSources_.empty()) {
            for (Level& level : levels_) {
                if (!level.active) {
                    const int32_t originX = level.targetOriginCell[0];
                    const int32_t originZ = level.targetOriginCell[1];
                    level = Level{};
                    level.active = true;
                    level.clearPending = true;
                    level.originCell[0] = level.targetOriginCell[0] = originX;
                    level.originCell[1] = level.targetOriginCell[1] = originZ;
                }
                level.idleSeconds = 0.0f;
            }
        }
        if (!HasActiveLevel()) {
            pendingSources_.clear();
            return;
        }

        Barrier::Transition(cmdList, surface_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        const int32_t noShift[2] = { 0, 0 };
        for (uint32_t index = 0; index < kLevelCount; ++index) {
            Level& level = levels_[index];
            if (!level.active) {
                continue;
            }
            if (level.clearPending) {
                Step(cmdList, index, noShift, true);
                level.clearPending = false;
            }
            InjectSources(cmdList, index);
            for (uint32_t step = 0; step < level.pendingSteps; ++step) {
                const int32_t shift[2] = {
                    level.targetOriginCell[0] - level.originCell[0],
                    level.targetOriginCell[1] - level.originCell[1] };
                Step(cmdList, index, shift, false);
                level.originCell[0] = level.targetOriginCell[0];
                level.originCell[1] = level.targetOriginCell[1];
            }
            level.pendingSteps = 0;
        }
        pendingSources_.clear();

        Barrier::Transition(cmdList, surface_, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    }

    void WaterRippleSimulation::InjectSources(ID3D12GraphicsCommandList* cmdList, uint32_t levelIndex)
    {
        if (pendingSources_.empty()) {
            return;
        }
        const Level& level = levels_[levelIndex];
        const float cellSize = kCellSizes[levelIndex];
        const float lowerBlur = LowerBlur(levelIndex);
        const float upperBlur = UpperBlur(levelIndex);
        const float widestBlur = (std::max)(lowerBlur, upperBlur);
        const float originX = static_cast<float>(level.originCell[0]) * cellSize;
        const float originZ = static_cast<float>(level.originCell[1]) * cellSize;

        WaterRippleInjectConstants constants{};
        int32_t rectMin[2] = { static_cast<int32_t>(kResolution), static_cast<int32_t>(kResolution) };
        int32_t rectMax[2] = { 0, 0 };
        // 物が起こした波紋の源を後ろへ並べる
        for (const bool fromObject : { false, true }) {
            if (fromObject) {
                constants.objectSourceBegin = constants.sourceCount;
            }
            for (const Source& source : pendingSources_) {
                if (source.fromObject != fromObject) {
                    continue;
                }
                const float reach = kSourceReachSigmas * std::sqrt(source.sigma * source.sigma + widestBlur * widestBlur);
                const int32_t minX = FloorToCell(source.x - reach - originX, cellSize);
                const int32_t minZ = FloorToCell(source.z - reach - originZ, cellSize);
                const int32_t maxX = FloorToCell(source.x + reach - originX, cellSize) + 1;
                const int32_t maxZ = FloorToCell(source.z + reach - originZ, cellSize) + 1;
                if (maxX <= 0 || maxZ <= 0
                    || minX >= static_cast<int32_t>(kResolution) || minZ >= static_cast<int32_t>(kResolution)) {
                    continue;
                }
                rectMin[0] = (std::min)(rectMin[0], (std::max)(minX, 0));
                rectMin[1] = (std::min)(rectMin[1], (std::max)(minZ, 0));
                rectMax[0] = (std::max)(rectMax[0], (std::min)(maxX, static_cast<int32_t>(kResolution)));
                rectMax[1] = (std::max)(rectMax[1], (std::min)(maxZ, static_cast<int32_t>(kResolution)));
                constants.sources[constants.sourceCount++] = Vector4{ source.x, source.z, source.sigma, source.depth };
            }
        }
        if (constants.sourceCount == 0 || rectMax[0] <= rectMin[0] || rectMax[1] <= rectMin[1]) {
            return;
        }

        constants.originXZ[0] = originX;
        constants.originXZ[1] = originZ;
        constants.cellSize = cellSize;
        constants.level = levelIndex;
        constants.pair = level.readPair;
        constants.lowerBlur = lowerBlur;
        constants.upperBlur = upperBlur;
        constants.rectMin[0] = rectMin[0];
        constants.rectMin[1] = rectMin[1];
        constants.rectSize[0] = static_cast<uint32_t>(rectMax[0] - rectMin[0]);
        constants.rectSize[1] = static_cast<uint32_t>(rectMax[1] - rectMin[1]);

        const D3D12_GPU_VIRTUAL_ADDRESS address = dxCommon_->GetUploadRing().AllocateConstants(constants);
        cmdList->SetPipelineState(injectPipeline_.Get());
        cmdList->SetComputeRootSignature(injectRootSignature_.GetRootSignature());
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(injectBindings_[InjectBind::WaterRippleInjectConstants], address);
        binder.Set(injectBindings_[InjectBind::gRippleState], stateUav_.gpuHandle);
        binder.ValidateBeforeDraw(injectBindings_);
        cmdList->Dispatch(
            DivRoundUp(constants.rectSize[0], kInjectGroupSize), DivRoundUp(constants.rectSize[1], kInjectGroupSize),
            kFieldCount);
        Barrier::UAV(cmdList, state_);
    }

    void WaterRippleSimulation::Step(
        ID3D12GraphicsCommandList* cmdList, uint32_t levelIndex, const int32_t shiftCells[2], bool clear)
    {
        Level& level = levels_[levelIndex];
        const float cellSize = kCellSizes[levelIndex];
        const float stepSeconds = kStepSeconds[levelIndex];

        WaterRipplePropagateConstants constants{};
        constants.shiftCells[0] = shiftCells[0];
        constants.shiftCells[1] = shiftCells[1];
        constants.level = levelIndex;
        constants.readPair = level.readPair;
        constants.courant = kGravity * stepSeconds * stepSeconds / cellSize;
        constants.keep = std::exp(-2.0f * dampingPerSecond_ * stepSeconds);
        constants.invTwoCellSize = 0.5f / cellSize;
        constants.clear = clear ? 1u : 0u;

        const D3D12_GPU_VIRTUAL_ADDRESS address = dxCommon_->GetUploadRing().AllocateConstants(constants);
        cmdList->SetPipelineState(propagatePipeline_.Get());
        cmdList->SetComputeRootSignature(propagateRootSignature_.GetRootSignature());
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(propagateBindings_[PropagateBind::WaterRipplePropagateConstants], address);
        binder.Set(propagateBindings_[PropagateBind::gRippleState], stateUav_.gpuHandle);
        binder.Set(propagateBindings_[PropagateBind::gRippleSurface], surfaceUav_.gpuHandle);
        binder.ValidateBeforeDraw(propagateBindings_);
        const UINT groups = DivRoundUp(kResolution, kPropagateGroupSize);
        cmdList->Dispatch(groups, groups, kFieldCount);

        BarrierBatch barriers(cmdList);
        barriers.UAV(state_);
        barriers.UAV(surface_);
        barriers.Flush();

        level.readPair = 1u - level.readPair;
    }

    void WaterRippleSimulation::FillBinding(WaterRippleBinding& out) const
    {
        out.enabled = 0;
        out.srvHeapIndex = isInitialized_ ? surfaceSrv_.index : 0u;
        for (uint32_t index = 0; index < kLevelCount; ++index) {
            const Level& level = levels_[index];
            const float cellSize = kCellSizes[index];
            out.originsXZ[index][0] = static_cast<float>(level.targetOriginCell[0]) * cellSize;
            out.originsXZ[index][1] = static_cast<float>(level.targetOriginCell[1]) * cellSize;
            const bool active = isInitialized_ && level.active;
            out.invSizes[index] = active ? 1.0f / (cellSize * static_cast<float>(kResolution)) : 0.0f;
            out.enabled |= active ? 1u : 0u;
        }
    }

    bool WaterRippleSimulation::HasActiveLevel() const
    {
        return std::any_of(levels_.begin(), levels_.end(), [](const Level& level) { return level.active; });
    }

    WaterRippleSimulation* FindWaterRippleSimulation(EngineSystem* engine)
    {
        RenderDomainContext* const domain = engine ? engine->GetRenderDomainContext() : nullptr;
        return domain ? domain->GetWaterRippleSimulation() : nullptr;
    }
}
