#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <cstdint>

#include "Graphics/Water/WaterSurfaceData.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"
#include "RayTracingOutputViewSet.h"
#include "WaterRayTracingPassBase.h"

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;
    class AccelerationStructureManager;

    struct WaterRefractionRayTracingSettings {
        float maxRayDistance = 500.0f;
        float waterRefractiveIndex = 1.333f;
        float surfaceBias = 0.02f;
        float absorptionCoeff = 0.3f;
        float maxRefractionOffsetPixels = 3.0f;
    };

    class WaterRefractionRayTracingManager : public WaterRayTracingPassBase {
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
            UINT width = 0;
            UINT height = 0;
            UINT blasCount = 0;
            UINT64 worldPositionSrv = 0;
            UINT64 sceneColorSrv = 0;
            UINT64 outputSrv = 0;
        };

        static constexpr uint32_t kViewCount = static_cast<uint32_t>(ViewID::Count);
        static_assert(kViewCount <= RayTracingOutputViewSet::kMaxViewCount,
            "WaterRefractionRayTracingManager: ViewID::Count exceeds RayTracingOutputViewSet::kMaxViewCount");

        bool Initialize(
            DirectXCommon* dxCommon,
            DescriptorManager* descriptorManager,
            AccelerationStructureManager* asMgr);

        void Dispatch(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE worldPositionSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV,
            const Matrix4x4& viewProjection,
            const Vector3& cameraPosition,
            const WaterSurfaceData& surfaceData,
            UINT width,
            UINT height,
            ViewID viewId = ViewID::GameView);

        void Resize(UINT width, UINT height, ViewID viewId = ViewID::GameView);

        D3D12_GPU_DESCRIPTOR_HANDLE GetRefractionSRVHandle(ViewID viewId = ViewID::GameView) const;
        ID3D12Resource* GetRefractionResource(ViewID viewId = ViewID::GameView) const;
        D3D12_RESOURCE_STATES& GetRefractionCurrentState(ViewID viewId = ViewID::GameView);

        bool IsInitialized() const { return isInitialized_; }

        void SetSurfaceModelProvider(const IWaterSurfaceModelProvider* provider);
        const IWaterSurfaceModelProvider* GetSurfaceModelProvider() const;

        void SetSettings(const WaterRefractionRayTracingSettings& settings) { settings_ = settings; }
        const WaterRefractionRayTracingSettings& GetSettings() const { return settings_; }
        const DispatchDiagnostics& GetLastDiagnostics() const { return lastDiagnostics_; }

    private:
        bool EnsureOutputTexture(UINT width, UINT height, uint32_t viewIndex);
        bool EnsureConstantBuffer();

        Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob_;
        WaterRefractionRayTracingSettings settings_{};
        DispatchDiagnostics lastDiagnostics_{};
    };
}
