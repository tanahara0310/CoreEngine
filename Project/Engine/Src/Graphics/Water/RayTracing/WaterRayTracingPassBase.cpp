#include "pch.h"
#include "WaterRayTracingPassBase.h"

#include <algorithm>
#include <cstring>

#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    bool WaterRayTracingPassBase::BeginDispatch(
        ID3D12GraphicsCommandList* cmdList,
        UINT width,
        UINT height,
        uint32_t viewIndex,
        DispatchResources& outResources,
        DXGI_FORMAT format)
    {
        return BeginDispatchBase(
            cmdList, width, height, viewIndex, outResources, format, GetSurfaceConstantBufferSize());
    }

    void WaterRayTracingPassBase::BeginDiagnostics(
        uint32_t viewIndex,
        UINT width,
        UINT height,
        const WaterSurfaceData& surfaceData,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV)
    {
        BeginDiagnosticsBase(viewIndex, width, height);

        lastWaterHeight_ = surfaceData.waterHeight;
        lastActiveWaveCount_ = surfaceData.activeWaveCount;

        // 水面固有の値は共通型の extras に載せる（デバッグ UI はパスの種類を知らずに表へ出せる）
        lastDispatchInfo_.AddExtra("waterHeight", surfaceData.waterHeight);
        lastDispatchInfo_.AddExtra("activeWaves", static_cast<float>(surfaceData.activeWaveCount));
        lastDispatchInfo_.AddExtra("sceneDepthSrv", sceneDepthSRV.ptr != 0 ? 1.0f : 0.0f);
        lastDispatchInfo_.AddExtra("sceneColorSrv", sceneColorSRV.ptr != 0 ? 1.0f : 0.0f);
    }

    WaterRayTracingPassBase::WaterSurfaceConstants WaterRayTracingPassBase::BuildSurfaceConstants(
        const WaterSurfaceData& surfaceData) const
    {
        WaterSurfaceConstants surfaceConstants{};
        surfaceConstants.waterHeight = surfaceData.waterHeight;
        surfaceConstants.activeWaveCount = (std::min)(surfaceData.activeWaveCount, kMaxWaterSurfaceWaveCount);
        surfaceConstants.time = surfaceData.time;
        surfaceConstants.simulationType = surfaceData.simulationType;
        for (uint32_t waveIndex = 0; waveIndex < surfaceConstants.activeWaveCount; ++waveIndex) {
            surfaceConstants.waves[waveIndex] = surfaceData.waves[waveIndex];
        }
        return surfaceConstants;
    }

    void WaterRayTracingPassBase::UploadSurfaceConstants(const WaterSurfaceConstants& surfaceConstants) const
    {
        if (!constantBufferMapped_) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "{}: surface constant upload skipped. constant buffer is not mapped.",
                GetOwnerName());
            return;
        }

        std::memcpy(constantBufferMapped_, &surfaceConstants, sizeof(surfaceConstants));
    }

    WaterRayTracingPassBase::WaterSurfaceConstants WaterRayTracingPassBase::UploadSurfaceDataForDispatch(
        const WaterSurfaceData& surfaceData) const
    {
        const WaterSurfaceConstants surfaceConstants = BuildSurfaceConstants(surfaceData);
        UploadSurfaceConstants(surfaceConstants);
        return surfaceConstants;
    }

    void WaterRayTracingPassBase::SetSurfaceModelProvider(
        const std::shared_ptr<const IWaterSurfaceModelProvider>& provider)
    {
        surfaceModelProvider_ = provider;
    }

    std::shared_ptr<const IWaterSurfaceModelProvider> WaterRayTracingPassBase::GetSurfaceModelProvider() const
    {
        return surfaceModelProvider_.lock();
    }

    const WaterSurfaceData& WaterRayTracingPassBase::ResolveSurfaceDataForDispatch(
        const WaterSurfaceData& fallbackSurfaceData,
        WaterSurfaceData& outResolvedSurfaceData) const
    {
        const std::shared_ptr<const IWaterSurfaceModelProvider> surfaceModelProvider = surfaceModelProvider_.lock();
        if (!surfaceModelProvider) {
            return fallbackSurfaceData;
        }

        if (!surfaceModelProvider->TryGetSurfaceData(outResolvedSurfaceData)) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: surface model provider returned no data. fallback path is used. provider='{}' type={}",
                GetOwnerName(),
                surfaceModelProvider->GetProviderName(),
                static_cast<uint32_t>(surfaceModelProvider->GetSimulationType()));
            return fallbackSurfaceData;
        }

        // 以前はここで毎フレーム Infof を出していたが、3 マネージャ分が常時流れて
        // ログを埋めるだけだったため撤去した（解決結果は GetDispatchInfo で参照できる）。
        return outResolvedSurfaceData;
    }
}
