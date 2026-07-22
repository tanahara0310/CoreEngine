#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <cstdint>
#include <memory>

#include "Graphics/Water/WaterSurfaceData.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"
#include "Graphics/RayTracing/RayTracingOutputViewSet.h"
#include "WaterRayTracingPassBase.h"

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;
    class AccelerationStructureManager;

    struct WaterReflectionRayTracingSettings {
        float maxRayDistance = 2000.0f;
        float surfaceBias = 0.05f;
        // 反射ヒット点のスクリーン再投影ずれ量の上限（ピクセル）。
        // 0 = 無制限（RT で求めた正確な位置をそのまま使う。既定）。
        float maxReflectionOffsetPixels = 0.0f;
        float debugDisplayScale = 1.0f;
        uint32_t debugViewMode = 0;
        uint32_t debugLogEnabled = 0;
    };

    /// @brief DXR による水面反射マネージャー。
    /// @details RTWaterRefractionRayTracingManager の対称形。反射レイをトレースし、
    ///          ヒット点を SceneColor へ再投影して水面反射カラーを生成する。
    ///          反射は GameView のみで必要なため ViewID は GameView 1 本。
    class WaterReflectionRayTracingManager : public WaterRayTracingPassBase {
    public:
        struct FFTOceanReflectionInput {
            D3D12_GPU_DESCRIPTOR_HANDLE displacementSRV{};
            D3D12_GPU_DESCRIPTOR_HANDLE normalSRV{};
            uint32_t resolution = 0;
            float patchLength = 0.0f;
            uint32_t enabled = 0;
            float uvScale[2] = { 0.0f, 0.0f };
            float uvOffset[2] = { 0.5f, 0.5f };
        };

        enum class ViewID : uint32_t {
            GameView = 0,
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
            UINT64 sceneDepthSrv = 0;
            UINT64 sceneColorSrv = 0;
            UINT64 outputSrv = 0;
        };

        static constexpr uint32_t kViewCount = static_cast<uint32_t>(ViewID::Count);
        static_assert(kViewCount <= RayTracingOutputViewSet::kMaxViewCount,
            "WaterReflectionRayTracingManager: ViewID::Count exceeds RayTracingOutputViewSet::kMaxViewCount");

        bool Initialize(
            DirectXCommon* dxCommon,
            DescriptorManager* descriptorManager,
            AccelerationStructureManager* asMgr);

        void Dispatch(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV,
            const Matrix4x4& viewProjection,
            const Vector3& cameraPosition,
            const WaterSurfaceData& surfaceData,
            const FFTOceanReflectionInput& fftOceanInput,
            UINT width,
            UINT height,
            ViewID viewId = ViewID::GameView);

        void Resize(UINT width, UINT height, ViewID viewId = ViewID::GameView);

        D3D12_GPU_DESCRIPTOR_HANDLE GetReflectionSRVHandle(ViewID viewId = ViewID::GameView) const;
        ID3D12Resource* GetReflectionResource(ViewID viewId = ViewID::GameView) const;
        D3D12_RESOURCE_STATES& GetReflectionCurrentState(ViewID viewId = ViewID::GameView);

        bool IsInitialized() const { return isInitialized_; }

        void SetSurfaceModelProvider(const std::shared_ptr<const IWaterSurfaceModelProvider>& provider);
        std::shared_ptr<const IWaterSurfaceModelProvider> GetSurfaceModelProvider() const;

        void SetSettings(const WaterReflectionRayTracingSettings& settings) { settings_ = settings; }
        const WaterReflectionRayTracingSettings& GetSettings() const { return settings_; }
        const DispatchDiagnostics& GetLastDiagnostics() const { return lastDiagnostics_; }

    private:
        bool EnsureOutputTexture(UINT width, UINT height, uint32_t viewIndex);
        bool EnsureConstantBuffer();

        Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob_;
        WaterReflectionRayTracingSettings settings_{};
        DispatchDiagnostics lastDiagnostics_{};
    };
}
