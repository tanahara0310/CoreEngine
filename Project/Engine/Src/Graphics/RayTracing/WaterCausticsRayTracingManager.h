#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <cstdint>

#include "Graphics/Water/WaterSurfaceData.h"
#include "Math/Vector/Vector3.h"
#include "RayTracingOutputViewSet.h"
#include "WaterRayTracingPassBase.h"

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;
    class AccelerationStructureManager;

    struct WaterCausticsRayTracingSettings {
        float maxTraceDistance = 500.0f;
        float surfaceBias = 0.02f;
        float intensityScale = 1.0f;
        float padding = 0.0f;
    };

    class WaterCausticsRayTracingManager : public WaterRayTracingPassBase {
    public:
        enum class ViewID : uint32_t {
            GameView = 0,
            ReflectionView = 1,
            Count
        };

        enum class DispatchStatus : uint32_t {
            None = 0,
            NotInitialized,
            RayTracingUnsupported,
            NoBLAS,
            OutputAllocationFailed,
            CommandList4Unavailable,
            Dispatched,
        };

        struct DispatchDiagnostics {
            DispatchStatus status = DispatchStatus::None;
            ViewID viewId = ViewID::GameView;
            float waterHeight = 0.0f;
            uint32_t activeWaveCount = 0;
            UINT width = 0;
            UINT height = 0;
            UINT blasCount = 0;
            UINT64 worldPositionSrv = 0;
            UINT64 outputSrv = 0;
        };

        static constexpr uint32_t kViewCount = static_cast<uint32_t>(ViewID::Count);
        static_assert(kViewCount <= RayTracingOutputViewSet::kMaxViewCount,
            "WaterCausticsRayTracingManager: ViewID::Count exceeds RayTracingOutputViewSet::kMaxViewCount");

        bool Initialize(
            DirectXCommon* dxCommon,
            DescriptorManager* descriptorManager,
            AccelerationStructureManager* asMgr);

        void Dispatch(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE worldPositionSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            const Vector3& lightDirection,
            const WaterSurfaceData& surfaceData,
            UINT width,
            UINT height,
            ViewID viewId = ViewID::GameView);

        void Resize(UINT width, UINT height, ViewID viewId = ViewID::GameView);

        D3D12_GPU_DESCRIPTOR_HANDLE GetCausticsSRVHandle(ViewID viewId = ViewID::GameView) const;
        ID3D12Resource* GetCausticsResource(ViewID viewId = ViewID::GameView) const;
        D3D12_RESOURCE_STATES& GetCausticsCurrentState(ViewID viewId = ViewID::GameView);

        bool IsInitialized() const { return isInitialized_; }

        void SetSurfaceModelProvider(const IWaterSurfaceModelProvider* provider);
        const IWaterSurfaceModelProvider* GetSurfaceModelProvider() const;

        void SetSettings(const WaterCausticsRayTracingSettings& settings) { settings_ = settings; }
        const WaterCausticsRayTracingSettings& GetSettings() const { return settings_; }
        const DispatchDiagnostics& GetLastDiagnostics() const { return lastDiagnostics_; }

    private:
        bool EnsureOutputTexture(UINT width, UINT height, uint32_t viewIndex);
        bool EnsureConstantBuffer();

        Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob_;
        WaterCausticsRayTracingSettings settings_{};
        DispatchDiagnostics lastDiagnostics_{};
    };
}
