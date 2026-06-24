#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <cstdint>

#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"
#include "GlobalRootSignatureManager.h"
#include "RayTracingPipelineBuilder.h"
#include "ShaderTableBuilder.h"

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;
    class AccelerationStructureManager;

    static constexpr uint32_t kMaxWaterRefractionWaveCount = 16;

    struct WaterRefractionWaveParam {
        float direction[2] = { 1.0f, 0.0f };
        float amplitude = 0.0f;
        float wavelength = 1.0f;
        float speed = 0.0f;
        float steepness = 0.0f;
        float phaseOffset = 0.0f;
        float padding = 0.0f;
    };

    struct WaterRefractionSurfaceData {
        float waterHeight = 0.0f;
        uint32_t activeWaveCount = 0;
        float time = 0.0f;
        float padding = 0.0f;
        WaterRefractionWaveParam waves[kMaxWaterRefractionWaveCount]{};
    };

    struct WaterRefractionRayTracingSettings {
        float maxRayDistance = 500.0f;
        float waterRefractiveIndex = 1.333f;
        float surfaceBias = 0.02f;
        float absorptionCoeff = 0.3f;
        float maxRefractionOffsetPixels = 3.0f;
    };

    class WaterRefractionRayTracingManager {
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
            const WaterRefractionSurfaceData& surfaceData,
            UINT width,
            UINT height,
            ViewID viewId = ViewID::GameView);

        void Resize(UINT width, UINT height, ViewID viewId = ViewID::GameView);

        D3D12_GPU_DESCRIPTOR_HANDLE GetRefractionSRVHandle(ViewID viewId = ViewID::GameView) const;
        ID3D12Resource* GetRefractionResource(ViewID viewId = ViewID::GameView) const;
        D3D12_RESOURCE_STATES& GetRefractionCurrentState(ViewID viewId = ViewID::GameView);

        bool IsInitialized() const { return isInitialized_; }

        void SetSettings(const WaterRefractionRayTracingSettings& settings) { settings_ = settings; }
        const WaterRefractionRayTracingSettings& GetSettings() const { return settings_; }
        const DispatchDiagnostics& GetLastDiagnostics() const { return lastDiagnostics_; }

    private:
        struct RefractionView {
            Microsoft::WRL::ComPtr<ID3D12Resource> texture;
            D3D12_GPU_DESCRIPTOR_HANDLE uavHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle{};
            D3D12_GPU_DESCRIPTOR_HANDLE srvHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{};
            UINT width = 0;
            UINT height = 0;
            D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        };

        bool EnsureOutputTexture(UINT width, UINT height, uint32_t viewIndex);
        bool EnsureConstantBuffer();

        DirectXCommon* dxCommon_ = nullptr;
        DescriptorManager* descriptorManager_ = nullptr;
        AccelerationStructureManager* asMgr_ = nullptr;

        Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob_;
        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
        uint8_t* constantBufferMapped_ = nullptr;
        GlobalRootSignatureManager globalRootSigMgr_;
        Microsoft::WRL::ComPtr<ID3D12StateObject> stateObject_;
        Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> stateObjectProperties_;
        ShaderTableBuilder shaderTableBuilder_;

        RefractionView views_[kViewCount]{};
        WaterRefractionRayTracingSettings settings_{};
        DispatchDiagnostics lastDiagnostics_{};
        bool isInitialized_ = false;
    };
}
