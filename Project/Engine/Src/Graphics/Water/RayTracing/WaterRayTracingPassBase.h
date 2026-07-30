#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <memory>
#include <string>

#include "Graphics/Water/WaterSurfaceData.h"
#include "Graphics/Water/Simulation/WaterSurfaceModelProvider.h"
#include "Graphics/RayTracing/RayTracingPassBase.h"

namespace CoreEngine
{
    /// @brief DXR 水面パス（屈折・反射・コースティクス）の共通基盤
    /// @details 出力ビュー管理・ディスパッチ前のガード判定・診断情報といった
    ///          「水面に限らない」部分は RayTracingPassBase へ移した（Stage 2a）。
    ///          ここに残るのは水面固有の要素だけ:
    ///            - 水面サーフェス定数（波・水面高さ・シミュレーション種別）の転送
    ///            - IWaterSurfaceModelProvider によるサーフェスデータ解決
    class WaterRayTracingPassBase : public RayTracingPassBase {
    public:
        /// @brief FFT Ocean の波面テクスチャ入力（3 パス共通）
        struct FFTOceanInput {
            D3D12_GPU_DESCRIPTOR_HANDLE displacementSRV{};
            D3D12_GPU_DESCRIPTOR_HANDLE normalSRV{};
            uint32_t resolution = 0;
            float patchLength = 0.0f;
            uint32_t enabled = 0;
        };

        /// @brief RT 側が参照する水面モデルの供給元を差し替える
        void SetSurfaceModelProvider(const std::shared_ptr<const IWaterSurfaceModelProvider>& provider);
        std::shared_ptr<const IWaterSurfaceModelProvider> GetSurfaceModelProvider() const;

        /// @brief 直近ディスパッチ時の水面高さ（診断ログ用）
        float GetLastWaterHeight() const { return lastWaterHeight_; }

        /// @brief 直近ディスパッチ時の有効波数（診断ログ用）
        uint32_t GetLastActiveWaveCount() const { return lastActiveWaveCount_; }

    protected:
        struct alignas(16) WaterSurfaceConstants {
            float waterHeight = 0.0f;
            uint32_t activeWaveCount = 0;
            float time = 0.0f;
            uint32_t simulationType = kWaterSurfaceModelTypeGerstner;
            WaterWaveParam waves[kMaxWaterSurfaceWaveCount]{};
        };

        /// @brief ディスパッチ前の共通処理（RayTracingPassBase::BeginDispatchBase の水面版）
        /// @details 水面サーフェス定数バッファのサイズを自動で渡す。
        bool BeginDispatch(
            ID3D12GraphicsCommandList* cmdList,
            UINT width,
            UINT height,
            uint32_t viewIndex,
            DispatchResources& outResources,
            DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT);

        /// @brief 診断情報の共通項目＋水面固有項目を初期化する（ディスパッチの冒頭で呼ぶ）
        void BeginDiagnostics(
            uint32_t viewIndex,
            UINT width,
            UINT height,
            const WaterSurfaceData& surfaceData,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV);

        static constexpr UINT GetSurfaceConstantBufferSize()
        {
            return (sizeof(WaterSurfaceConstants) + 255) & ~255;
        }
        WaterSurfaceConstants UploadSurfaceDataForDispatch(const WaterSurfaceData& surfaceData) const;

        /// @brief provider があればそこから、無ければ fallback から surface data を決める
        const WaterSurfaceData& ResolveSurfaceDataForDispatch(
            const WaterSurfaceData& fallbackSurfaceData,
            WaterSurfaceData& outResolvedSurfaceData) const;

        std::weak_ptr<const IWaterSurfaceModelProvider> surfaceModelProvider_;

    private:
        WaterSurfaceConstants BuildSurfaceConstants(const WaterSurfaceData& surfaceData) const;
        void UploadSurfaceConstants(const WaterSurfaceConstants& surfaceConstants) const;

        float lastWaterHeight_ = 0.0f;
        uint32_t lastActiveWaveCount_ = 0;
    };
}
