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
    class GraphicsCore;
    class DescriptorAllocator;
    class AccelerationStructureManager;

    /// @brief RT 反射の設定
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
        // ビュー識別子は 3 マネージャ共通（WaterRayTracingPassBase.h の RTWaterViewID）。
        // 反射が実際に使うのは GameView のみ（出力スロットは使用したビューだけ確保される）。
        using ViewID = RTWaterViewID;

        static constexpr uint32_t kViewCount = static_cast<uint32_t>(ViewID::Count);
        static_assert(kViewCount <= RayTracingOutputViewSet::kMaxSlotCount,
            "WaterReflectionRayTracingManager: ViewID::Count exceeds RayTracingOutputViewSet::kMaxSlotCount");

        bool Initialize(
            GraphicsCore* dxCommon,
            DescriptorAllocator* descriptorAllocator,
            AccelerationStructureManager* asMgr);

        void Dispatch(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV,
            const Matrix4x4& viewProjection,
            const Vector3& cameraPosition,
            const WaterSurfaceData& surfaceData,
            const FFTOceanInput& fftOceanInput,
            /// 空キューブマップ（AtmosphereManager::GetSkySpecularSRVHandle）。
            /// ptr==0 なら空を解決せず理由コードを返し、Water.PS の保険が動く。
            D3D12_GPU_DESCRIPTOR_HANDLE skyEnvironmentSRV,
            UINT width,
            UINT height,
            ViewID viewId = ViewID::GameView);

        /// @brief 出力テクスチャを指定サイズで作り直す
        void Resize(UINT width, UINT height, ViewID viewId = ViewID::GameView);

        /// @brief 反射出力テクスチャの SRV ハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetReflectionSRVHandle(ViewID viewId = ViewID::GameView) const;
        /// @brief 反射出力テクスチャのリソース
        ID3D12Resource* GetReflectionResource(ViewID viewId = ViewID::GameView) const;
        /// @brief 反射出力の現在ステートへの参照（バリア時に更新される）
        D3D12_RESOURCE_STATES& GetReflectionCurrentState(ViewID viewId = ViewID::GameView);

        void SetSettings(const WaterReflectionRayTracingSettings& settings) { settings_ = settings; }
        const WaterReflectionRayTracingSettings& GetSettings() const { return settings_; }

    private:
        WaterReflectionRayTracingSettings settings_{};
    };
}
