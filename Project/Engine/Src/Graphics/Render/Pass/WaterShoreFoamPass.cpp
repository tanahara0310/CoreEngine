#include "pch.h"
#include "WaterShoreFoamPass.h"

#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Water/FFTOceanManager.h"
#include "Graphics/Water/Foam/WaterFoamSystem.h"
#include "Graphics/Water/RayTracing/WaterSeabedRayTracingManager.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    static_assert(WaterFoamSystem::kShoreResolution * WaterFoamSystem::kShoreTexelSize
            == WaterSeabedRayTracingManager::kResolution * WaterSeabedRayTracingManager::kTexelSize,
        "岸の泡の格子は海底の高さと同じ範囲を覆う");
    static_assert(WaterSeabedRayTracingManager::kWindowSnapMeters == WaterFoamSystem::kShoreTexelSize,
        "範囲の角は岸の泡の格子に揃う");

    void WaterShoreFoamPass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::RTWaterSeabedHeight, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::WaterShoreFoam, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void WaterShoreFoamPass::Execute(const RenderContext& context)
    {
        if (!context.waterSurfaceState || context.waterSurfaceState->regionValid == 0 || !context.cmdList) {
            return;
        }
        auto* foam = context.waterFoamSystem;
        auto* seabed = context.rtWaterSeabedManager;
        auto* fftOcean = context.fftOceanManager;
        if (!foam || !foam->IsShoreActive() || !seabed || !seabed->IsInitialized()
            || !fftOcean || !fftOcean->IsInitialized()) {
            return;
        }

        constexpr auto kView = WaterSeabedRayTracingManager::ViewID::GameView;
        const WaterSeabedWindow& window = seabed->GetWindow();
        const D3D12_GPU_DESCRIPTOR_HANDLE seabedSRV = seabed->GetSeabedSRVHandle(kView);
        if (!window.valid || seabedSRV.ptr == 0) {
            return;
        }

        WaterFoamSystem::ShoreInput input{};
        input.seabedHeightSRV = seabedSRV;
        input.seabedResolution = WaterSeabedRayTracingManager::kResolution;
        input.windowOriginXZ[0] = window.originX;
        input.windowOriginXZ[1] = window.originZ;
        input.windowSize = window.size;
        input.fftDisplacementSRV = fftOcean->GetDisplacementSRVHandle();
        input.waterRestHeight = context.waterSurfaceState->waterHeight;
        input.timeSeconds = context.fftOceanSimulationTime;
        input.spectrumRevision = fftOcean->GetSpectrumRevision();
        input.frameNumber = context.frameNumber;
        const std::array<float, 3> waveGroupPhase = fftOcean->ComputeWaveGroupPhase(context.fftOceanSimulationTime);
        for (size_t i = 0; i < waveGroupPhase.size(); ++i) {
            input.waveGroupPhase[i] = waveGroupPhase[i];
        }
        foam->DispatchShore(context.cmdList, input);

        if (context.frameBlackboard) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::WaterShoreFoam,
                foam->GetLatestShoreSRVHandle(),
                &foam->GetLatestShoreResource());
        }
    }
}
