#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <cstdint>
#include <memory>

#include "Graphics/Water/WaterSurfaceData.h"
#include "Math/Vector/Vector3.h"
#include "Graphics/RayTracing/RayTracingOutputViewSet.h"
#include "WaterRayTracingPassBase.h"

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;
    class AccelerationStructureManager;

    struct WaterCausticsRayTracingSettings {
        float maxTraceDistance = 500.0f;
        float surfaceBias = 0.02f;
        float intensityScale = 3.0f;
        float refractiveIndex = 1.333f;
        float debugDisplayScale = 1.0f;
        uint32_t debugViewMode = 0;
        uint32_t debugLogEnabled = 0;
    };

    class WaterCausticsRayTracingManager : public WaterRayTracingPassBase {
    public:
        struct FFTOceanCausticsInput {
            D3D12_GPU_DESCRIPTOR_HANDLE displacementSRV{};
            D3D12_GPU_DESCRIPTOR_HANDLE normalSRV{};
            uint32_t resolution = 0;
            float patchLength = 0.0f;
            uint32_t enabled = 0;
            // ワールドXZ → FFT テクスチャ UV の写像（uv = worldXZ * scale + offset）。
            // ラスタ描画（FFTWater.VS）と同じ波面を評価するために、水面メッシュの
            // 位置・スケールから導出した値を渡す
            float uvScale[2] = { 0.0f, 0.0f };
            float uvOffset[2] = { 0.5f, 0.5f };
        };

        // シーンの実際のディレクショナルライト情報。
        // 以前は方向のみを受け取っており、色・強度・有効フラグが RT コースティクスへ
        // 一切反映されていなかった（ライトを消してもコースティクスが消えない、
        // ライト色を変えても常に白いままになる不具合の原因）。
        struct LightInput {
            Vector3 direction{ 0.0f, -1.0f, 0.0f };
            Vector3 color{ 1.0f, 1.0f, 1.0f };
            float intensity = 1.0f;
            bool enabled = true;
        };

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
            const LightInput& lightInput,
            const WaterSurfaceData& surfaceData,
            const FFTOceanCausticsInput& fftOceanInput,
            UINT width,
            UINT height,
            ViewID viewId = ViewID::GameView);

        void Resize(UINT width, UINT height, ViewID viewId = ViewID::GameView);

        D3D12_GPU_DESCRIPTOR_HANDLE GetCausticsSRVHandle(ViewID viewId = ViewID::GameView) const;
        ID3D12Resource* GetCausticsResource(ViewID viewId = ViewID::GameView) const;
        D3D12_RESOURCE_STATES& GetCausticsCurrentState(ViewID viewId = ViewID::GameView);

        bool IsInitialized() const { return isInitialized_; }

        void SetSurfaceModelProvider(const std::shared_ptr<const IWaterSurfaceModelProvider>& provider);
        std::shared_ptr<const IWaterSurfaceModelProvider> GetSurfaceModelProvider() const;

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
