#include "pch.h"
#include "WaterSpraySimulatePass.h"

#include <algorithm>

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Water/RayTracing/WaterRayTracingPassBase.h"
#include "Graphics/Water/RayTracing/WaterSeabedRayTracingManager.h"
#include "Graphics/Water/Shore/WaterShoreWaveSystem.h"
#include "Graphics/Water/Spray/WaterSpraySystem.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    namespace
    {
        /// @brief 1 フレームに進める時間の上限 [s]
        constexpr float kMaxSprayDeltaTime = 1.0f / 20.0f;
    }

    void WaterSpraySimulatePass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::RTWaterSeabedHeight, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::WaterShoreWave,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::WaterSprayParticles, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    void WaterSpraySimulatePass::Execute(const RenderContext& context)
    {
        // 波の時計で進んだ時間（止まっている間と巻き戻ったときは 0）
        const float simulationTime = context.fftOceanSimulationTime;
        float deltaTime = 0.0f;
        if (hasLastSimulationTime_) {
            deltaTime = std::clamp(simulationTime - lastSimulationTime_, 0.0f, kMaxSprayDeltaTime);
        }
        lastSimulationTime_ = simulationTime;
        hasLastSimulationTime_ = true;

        const WaterSurfaceData* surface = context.waterSurfaceState;
        auto* spray = context.waterSpraySystem;
        auto* shoreWave = context.waterShoreWaveSystem;
        auto* seabed = context.rtWaterSeabedManager;
        if (!surface || surface->regionValid == 0 || !context.cmdList || !context.dxCommon
            || !spray || !spray->IsActive() || !shoreWave || !shoreWave->IsActive() || !shoreWave->HasField()
            || !seabed || !seabed->IsInitialized()) {
            return;
        }

        const WaterRayTracingPassBase::FFTOceanInput fftInput = WaterRayTracingPassBase::BuildFFTOceanInput(
            context.fftOceanManager, shoreWave, *surface, context.fftOceanSimulationTime);
        const WaterSeabedWindow& window = seabed->GetWindow();
        if (fftInput.enabled == 0 || fftInput.shoreWaveFieldSRV.ptr == 0 || !window.valid) {
            return;
        }

        const WaterRayTracingPassBase::WaterSurfaceConstants surfaceConstants =
            WaterRayTracingPassBase::BuildSurfaceConstants(*surface, fftInput);

        WaterSpraySystem::SimulateInput input{};
        input.surfaceConstants = context.dxCommon->GetUploadRing().AllocateConstants(surfaceConstants);
        input.fftDisplacementSRV = fftInput.displacementSRV;
        input.shoreWaveFieldSRV = fftInput.shoreWaveFieldSRV;
        input.seabedHeightSRV = seabed->GetSeabedSRVHandle(WaterSeabedRayTracingManager::ViewID::GameView);
        input.windowOriginXZ[0] = window.originX;
        input.windowOriginXZ[1] = window.originZ;
        input.windowSize = window.size;
        input.deltaTime = deltaTime;
        input.frameNumber = context.frameNumber;
        spray->Simulate(context.cmdList, input);

        if (context.frameBlackboard && spray->HasParticles()) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::WaterSprayParticles,
                spray->GetParticleSRVHandle(),
                &spray->GetParticleResource());
        }
    }
}
