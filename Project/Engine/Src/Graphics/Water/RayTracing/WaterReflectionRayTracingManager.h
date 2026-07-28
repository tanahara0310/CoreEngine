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
        enum class ViewID : uint32_t {
            GameView = 0,
            Count
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
            const FFTOceanInput& fftOceanInput,
            UINT width,
            UINT height,
            ViewID viewId = ViewID::GameView);

        void Resize(UINT width, UINT height, ViewID viewId = ViewID::GameView);

        D3D12_GPU_DESCRIPTOR_HANDLE GetReflectionSRVHandle(ViewID viewId = ViewID::GameView) const;
        ID3D12Resource* GetReflectionResource(ViewID viewId = ViewID::GameView) const;
        D3D12_RESOURCE_STATES& GetReflectionCurrentState(ViewID viewId = ViewID::GameView);

        void SetSettings(const WaterReflectionRayTracingSettings& settings) { settings_ = settings; }
        const WaterReflectionRayTracingSettings& GetSettings() const { return settings_; }

    private:
        Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob_;
        WaterReflectionRayTracingSettings settings_{};
    };
}
