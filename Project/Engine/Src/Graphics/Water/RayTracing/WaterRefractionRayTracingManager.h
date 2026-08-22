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

    /// @brief RT 屈折の設定
    struct WaterRefractionRayTracingSettings {
        float maxRayDistance = 500.0f;
        float waterRefractiveIndex = 1.333f;
        float surfaceBias = 0.02f;
        float absorptionCoeff = 0.3f;
        // 屈折ヒット点のスクリーン再投影ずれ量の上限（ピクセル）。
        // 0 = 無制限（RT で求めた正確な位置をそのまま使う。既定）。
        // 正の値を指定した場合のみ暴発防止の安全クランプとして機能する。
        float maxRefractionOffsetPixels = 0.0f;
        float debugDisplayScale = 1.0f;
        uint32_t debugViewMode = 0;
        uint32_t debugLogEnabled = 0;
    };

    /// @brief 水面の屈折をレイトレーシングで生成するマネージャ
    class WaterRefractionRayTracingManager : public WaterRayTracingPassBase {
    public:
        // ビュー識別子は 3 マネージャ共通（WaterRayTracingPassBase.h の RTWaterViewID）
        using ViewID = RTWaterViewID;

        static constexpr uint32_t kViewCount = static_cast<uint32_t>(ViewID::Count);
        static_assert(kViewCount <= RayTracingOutputViewSet::kMaxSlotCount,
            "WaterRefractionRayTracingManager: ViewID::Count exceeds RayTracingOutputViewSet::kMaxSlotCount");

        bool Initialize(
            GraphicsCore* dxCommon,
            DescriptorAllocator* descriptorAllocator,
            AccelerationStructureManager* asMgr);

        void Dispatch(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV, // WorldPosition ターゲット廃止に伴い深度から復元する
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV,
            const Matrix4x4& viewProjection,
            const Vector3& cameraPosition,
            const WaterSurfaceData& surfaceData,
            const FFTOceanInput& fftOceanInput,
            UINT width,
            UINT height,
            ViewID viewId = ViewID::GameView);

        /// @brief 出力テクスチャを指定サイズで作り直す
        void Resize(UINT width, UINT height, ViewID viewId = ViewID::GameView);

        /// @brief 屈折出力テクスチャの SRV ハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetRefractionSRVHandle(ViewID viewId = ViewID::GameView) const;
        /// @brief 屈折出力テクスチャのリソース
        ID3D12Resource* GetRefractionResource(ViewID viewId = ViewID::GameView) const;
        /// @brief 屈折出力の現在ステートへの参照（バリア時に更新される）
        D3D12_RESOURCE_STATES& GetRefractionCurrentState(ViewID viewId = ViewID::GameView);

        void SetSettings(const WaterRefractionRayTracingSettings& settings) { settings_ = settings; }
        const WaterRefractionRayTracingSettings& GetSettings() const { return settings_; }

    private:
        WaterRefractionRayTracingSettings settings_{};
    };
}
