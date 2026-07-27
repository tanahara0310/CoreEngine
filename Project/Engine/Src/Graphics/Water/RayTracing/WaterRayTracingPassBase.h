#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <memory>
#include <string>

#include "Graphics/Water/WaterSurfaceData.h"
#include "Graphics/Water/Simulation/WaterSurfaceModelProvider.h"
#include "Graphics/RayTracing/GlobalRootSignatureManager.h"
#include "Graphics/RayTracing/RayTracingOutputViewSet.h"
#include "Graphics/RayTracing/ShaderTableBuilder.h"

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;
    class AccelerationStructureManager;

    /// @brief DXR 水面パス（屈折・反射・コースティクス）の共通基盤
    /// @details 3 つのマネージャは「水面をレイでたどって 1 枚の出力テクスチャを作る」点で
    ///          同型なので、出力ビュー管理・サーフェス定数の転送・ディスパッチ前の
    ///          ガード判定・診断情報をここへ集約する。
    ///          派生側に残すのは「シェーダー・ルートシグネチャ・固有の定数バッファ」だけ。
    class WaterRayTracingPassBase {
    public:
        /// @brief FFT Ocean の波面テクスチャ入力（3 パス共通）
        struct FFTOceanInput {
            D3D12_GPU_DESCRIPTOR_HANDLE displacementSRV{};
            D3D12_GPU_DESCRIPTOR_HANDLE normalSRV{};
            uint32_t resolution = 0;
            float patchLength = 0.0f;
            uint32_t enabled = 0;
        };

        /// @brief ディスパッチの結果状態
        enum class DispatchStatus : uint32_t {
            None = 0,
            NotInitialized,
            RayTracingUnsupported,
            NoBLAS,
            OutputAllocationFailed,
            CommandList4Unavailable,
            Dispatched,
        };

        /// @brief 直近のディスパッチの診断情報
        /// @note ビューは派生ごとに enum が異なるため、ここでは添字で保持する。
        struct DispatchDiagnostics {
            DispatchStatus status = DispatchStatus::None;
            uint32_t viewIndex = 0;
            float waterHeight = 0.0f;
            uint32_t activeWaveCount = 0;
            UINT width = 0;
            UINT height = 0;
            UINT blasCount = 0;
            UINT64 sceneDepthSrv = 0;
            UINT64 sceneColorSrv = 0;
            UINT64 outputSrv = 0;
        };

        static const char* ToString(DispatchStatus status);

        bool IsInitialized() const { return isInitialized_; }

        /// @brief RT 側が参照する水面モデルの供給元を差し替える
        void SetSurfaceModelProvider(const std::shared_ptr<const IWaterSurfaceModelProvider>& provider);
        std::shared_ptr<const IWaterSurfaceModelProvider> GetSurfaceModelProvider() const;

        const DispatchDiagnostics& GetLastDiagnostics() const { return lastDiagnostics_; }

    protected:
        struct alignas(16) WaterSurfaceConstants {
            float waterHeight = 0.0f;
            uint32_t activeWaveCount = 0;
            float time = 0.0f;
            uint32_t simulationType = kWaterSurfaceModelTypeGerstner;
            WaterWaveParam waves[kMaxWaterSurfaceWaveCount]{};
        };

        enum class DispatchGuardStatus : uint32_t {
            Ok = 0,
            NotInitialized,
            RayTracingUnsupported,
            NoBLAS,
            InvalidCommandList,
            CommandList4Unavailable,
            OutputAllocationFailed,
        };

        /// @brief BeginDispatch が用意する、ディスパッチ 1 回分のリソース一式
        struct DispatchResources {
            D3D12_GPU_DESCRIPTOR_HANDLE outputSrvHandle{};
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle{};
            ID3D12Resource* outputResource = nullptr;
            D3D12_RESOURCE_STATES* outputCurrentState = nullptr;
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        };

        /// @param ownerName        ログに出す所有者名（以後 ownerName_ として使い回す）
        /// @param outputDebugName  出力テクスチャのデバッグ名の接頭辞
        bool InitializeBase(
            DirectXCommon* dxCommon,
            DescriptorManager* descriptorManager,
            AccelerationStructureManager* asMgr,
            const char* ownerName,
            const char* outputDebugName);

        /// @brief ディスパッチ前の共通処理をまとめて行う
        /// @details ガード判定 → 出力テクスチャ／定数バッファの確保 → CommandList4 取得。
        ///          失敗時は lastDiagnostics_.status を埋めて警告ログまで出すので、
        ///          呼び出し側は false なら即 return してよい。
        /// @return 成功したか
        bool BeginDispatch(
            ID3D12GraphicsCommandList* cmdList,
            UINT width,
            UINT height,
            uint32_t viewIndex,
            DispatchResources& outResources,
            DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT);

        /// @brief 診断情報の共通項目を初期化する（ディスパッチの冒頭で呼ぶ）
        void BeginDiagnostics(
            uint32_t viewIndex,
            UINT width,
            UINT height,
            const WaterSurfaceData& surfaceData,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV);

        bool EnsureSurfaceConstantBuffer(UINT bufferSize);
        void ReleaseOutputIfSizeMismatchBase(UINT width, UINT height, uint32_t viewIndex);

        D3D12_GPU_DESCRIPTOR_HANDLE GetOutputSRVHandleBase(uint32_t viewIndex) const;
        ID3D12Resource* GetOutputResourceBase(uint32_t viewIndex) const;
        D3D12_RESOURCE_STATES& GetOutputCurrentStateBase(uint32_t viewIndex);

        void BeginOutputWrite(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* outputResource,
            D3D12_RESOURCE_STATES& outputCurrentState) const;
        void EndOutputWrite(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* outputResource,
            D3D12_RESOURCE_STATES& outputCurrentState,
            D3D12_RESOURCE_STATES finalState) const;

        static constexpr UINT GetSurfaceConstantBufferSize()
        {
            return (sizeof(WaterSurfaceConstants) + 255) & ~255;
        }
        WaterSurfaceConstants UploadSurfaceDataForDispatch(const WaterSurfaceData& surfaceData) const;

        /// @brief provider があればそこから、無ければ fallback から surface data を決める
        const WaterSurfaceData& ResolveSurfaceDataForDispatch(
            const WaterSurfaceData& fallbackSurfaceData,
            WaterSurfaceData& outResolvedSurfaceData) const;

        const char* GetOwnerName() const { return ownerName_; }

        DirectXCommon* dxCommon_ = nullptr;
        DescriptorManager* descriptorManager_ = nullptr;
        AccelerationStructureManager* asMgr_ = nullptr;

        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
        uint8_t* constantBufferMapped_ = nullptr;
        GlobalRootSignatureManager globalRootSigMgr_;
        Microsoft::WRL::ComPtr<ID3D12StateObject> stateObject_;
        Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> stateObjectProperties_;
        ShaderTableBuilder shaderTableBuilder_;
        RayTracingOutputViewSet outputViews_;
        std::weak_ptr<const IWaterSurfaceModelProvider> surfaceModelProvider_;

        DispatchDiagnostics lastDiagnostics_{};
        bool isInitialized_ = false;

    private:
        WaterSurfaceConstants BuildSurfaceConstants(const WaterSurfaceData& surfaceData) const;
        void UploadSurfaceConstants(const WaterSurfaceConstants& surfaceConstants) const;
        DispatchGuardStatus ValidateDispatchPreconditions(ID3D12GraphicsCommandList* cmdList) const;
        bool QueryCommandList4(
            ID3D12GraphicsCommandList* cmdList,
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4>& cmdList4) const;
        bool EnsureOutputTextureBase(
            UINT width,
            UINT height,
            uint32_t viewIndex,
            DXGI_FORMAT format);
        /// @brief ガード失敗を diagnostics へ反映し、警告ログを出す
        void ReportDispatchGuardFailure(DispatchGuardStatus guardStatus, ID3D12GraphicsCommandList* cmdList);

        const char* ownerName_ = "WaterRayTracingPassBase";
        const char* outputDebugName_ = "RTWaterOutput";
    };
}
