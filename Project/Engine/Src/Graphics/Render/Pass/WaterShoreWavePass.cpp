#include "pch.h"
#include "WaterShoreWavePass.h"

#include "Graphics/Render/RenderGraph.h"
#include "Graphics/Water/RayTracing/WaterSeabedRayTracingManager.h"
#include "Graphics/Water/Shore/WaterShoreWaveSystem.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    static_assert(WaterSeabedRayTracingManager::kResolution * WaterSeabedRayTracingManager::kTexelSize
            == WaterShoreWaveSystem::kResolution * WaterSeabedRayTracingManager::kWindowSnapMeters,
        "範囲の角は岸の波の場の格子に揃う（場の 1 テクセル = 範囲のスナップ幅）");

    namespace
    {
        /// @brief 場はレイトレの水面（次のフレームのコースティクスを含む）とピクセルシェーダーの両方が読む
        constexpr D3D12_RESOURCE_STATES kAnyShaderResource =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    void WaterShoreWavePass::DeclareResources(RenderGraphBuilder& builder, [[maybe_unused]] const RenderContext& context)
    {
        builder.Read(FrameBlackboard::RTWaterSeabedHeight, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        builder.Write(FrameBlackboard::WaterShoreWave, kAnyShaderResource);
    }

    void WaterShoreWavePass::Execute(const RenderContext& context)
    {
        if (!context.waterSurfaceState || context.waterSurfaceState->regionValid == 0 || !context.cmdList) {
            return;
        }
        auto* shoreWave = context.waterShoreWaveSystem;
        auto* seabed = context.rtWaterSeabedManager;
        if (!shoreWave || !shoreWave->IsActive() || !seabed || !seabed->IsInitialized()) {
            return;
        }

        constexpr auto kView = WaterSeabedRayTracingManager::ViewID::GameView;
        const WaterSeabedWindow& window = seabed->GetWindow();
        const D3D12_GPU_DESCRIPTOR_HANDLE seabedSRV = seabed->GetSeabedSRVHandle(kView);
        if (!window.valid || seabedSRV.ptr == 0) {
            return;
        }

        WaterShoreWaveSystem::Input input{};
        input.seabedHeightSRV = seabedSRV;
        input.windowOriginXZ[0] = window.originX;
        input.windowOriginXZ[1] = window.originZ;
        input.windowSize = window.size;
        input.waterRestHeight = context.waterSurfaceState->waterHeight;
        input.frameNumber = context.frameNumber;
        shoreWave->Dispatch(context.cmdList, input);

        if (context.frameBlackboard && shoreWave->HasField()) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::WaterShoreWave,
                shoreWave->GetFieldSRVHandle(),
                &shoreWave->GetFieldResource());
        }
    }
}
