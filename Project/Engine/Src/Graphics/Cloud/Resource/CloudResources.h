#pragma once

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"

#include <cstdint>
#include <d3d12.h>

namespace CoreEngine
{
    class DescriptorAllocator;
    class GraphicsCore;

    /// @brief リソース本体・追跡ステート・SRV/UAV をひとまとめにした束
    struct CloudGpuTexture : GpuResource {
        DescriptorHandle srv{};
        DescriptorHandle uav{};
    };

    /// @brief 雲が使う GPU テクスチャ一式の生成と保持
    /// @details 起動時に確保する静的リソースと、SceneColor 追従で作り直すフレームターゲットに分かれる。
    class CloudResources {
    public:
        // ノイズ解像度（HLSL 側 CS の Dispatch と一致させること）
        static constexpr uint32_t kBaseShapeNoiseSize = 128;
        static constexpr uint32_t kDetailNoiseSize = 32;
        static constexpr uint32_t kWeatherMapSize = 512;
        // 雲シャドウマップ解像度（HLSL 側 GodRayCommon.hlsli の定数と一致させること）
        static constexpr uint32_t kCloudShadowMapSize = 1024;

        /// @brief ノイズ 3 枚を確保する
        bool CreateNoiseTextures(ID3D12Device* device, DescriptorAllocator* descriptorAllocator);

        /// @brief 雲シャドウマップを確保する（ゴッドレイ用）
        bool CreateCloudShadowMap(ID3D12Device* device, DescriptorAllocator* descriptorAllocator);

        /// @brief SceneColor のサイズと分割数に追従してフレームターゲットを確保する
        /// @param graphicsCore 作り直す前の GPU 完了待ちに使う（nullptr でも動くが待たない）
        /// @param sceneColor サイズとフォーマットの基準
        /// @param resolutionDivisor 半解像度バッファの分割数（1 以上）
        /// @return 確保済み（または再利用可能）なら true
        bool EnsureFrameTargets(ID3D12Device* device,
                                DescriptorAllocator* descriptorAllocator,
                                GraphicsCore* graphicsCore,
                                GpuResource& sceneColor,
                                uint32_t resolutionDivisor);

        // ===== 静的リソース =====
        CloudGpuTexture baseShapeNoise;
        CloudGpuTexture detailNoise;
        CloudGpuTexture weatherMap;
        CloudGpuTexture cloudShadowMap;

        // ===== フレームターゲット（EnsureFrameTargets が確保する） =====
        CloudGpuTexture cloudBuffer;      ///< 半解像度レイマーチ結果
        CloudGpuTexture godRayBuffer;     ///< 半解像度ゴッドレイ結果

        /// @brief 半解像度バッファの実サイズ
        uint32_t TargetsWidth() const { return targetsWidth_; }
        uint32_t TargetsHeight() const { return targetsHeight_; }

    private:
        uint32_t targetsWidth_ = 0;
        uint32_t targetsHeight_ = 0;
    };
}
