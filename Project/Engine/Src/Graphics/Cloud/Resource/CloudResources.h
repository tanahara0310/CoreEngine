#pragma once

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"

#include <cstdint>
#include <d3d12.h>
#include <vector>

namespace CoreEngine
{
    class DescriptorAllocator;
    class GraphicsCore;

    /// @brief リソース本体・追跡ステート・SRV/UAV をひとまとめにした束
    /// @details ミップを持つテクスチャは段ごとのビューを併せ持つ。
    ///          srv は全段を見る（サンプル用）、uav は段 0 を指す。
    struct CloudGpuTexture : GpuResource {
        DescriptorHandle srv{};
        DescriptorHandle uav{};
        std::vector<DescriptorHandle> mipSrvs;
        std::vector<DescriptorHandle> mipUavs;

        uint32_t MipLevels() const noexcept { return static_cast<uint32_t>(mipUavs.size()); }
    };

    /// @brief 雲が使う GPU テクスチャ一式の生成と保持
    /// @details 起動時に確保する静的リソースと、SceneColor 追従で作り直すフレームターゲットに分かれる。
    class CloudResources {
    public:
        // ノイズ解像度（HLSL 側 CS の Dispatch と一致させること）
        static constexpr uint32_t kBaseShapeNoiseSize = 128;
        static constexpr uint32_t kDetailNoiseSize = 32;
        static constexpr uint32_t kWeatherMapSize = 512;
        // 3D ノイズのミップ段数。マーチのステップ幅から求める LOD がこの範囲に収まること
        static constexpr uint32_t kNoiseMipLevels = 5;
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
        /// @param outRecreated 作り直したときに true（履歴が無効になったことを呼び出し側へ伝える）
        /// @return 確保済み（または再利用可能）なら true
        bool EnsureFrameTargets(ID3D12Device* device,
                                DescriptorAllocator* descriptorAllocator,
                                GraphicsCore* graphicsCore,
                                GpuResource& sceneColor,
                                uint32_t resolutionDivisor,
                                bool* outRecreated = nullptr);

        // ===== 静的リソース =====
        CloudGpuTexture baseShapeNoise;
        CloudGpuTexture detailNoise;
        CloudGpuTexture weatherMap;
        CloudGpuTexture cloudShadowMap;

        // ===== フレームターゲット（EnsureFrameTargets が確保する） =====
        CloudGpuTexture cloudBuffers[2];  ///< 半解像度レイマーチ結果（時間再投影の ping-pong）
        CloudGpuTexture godRayBuffer;     ///< 半解像度ゴッドレイ結果

        /// @brief 今フレームの書き込み先
        CloudGpuTexture& CurrentCloudBuffer() { return cloudBuffers[writeIndex_]; }

        /// @brief 前フレームのレイマーチ結果（履歴）
        CloudGpuTexture& HistoryCloudBuffer() { return cloudBuffers[writeIndex_ ^ 1]; }

        /// @brief 書き込み先をフレーム番号の偶奇で決める（純粋関数。累積状態を持たない）
        void SetFrameIndex(uint32_t frameIndex) { writeIndex_ = frameIndex & 1u; }

        /// @brief 半解像度バッファの実サイズ
        uint32_t TargetsWidth() const { return targetsWidth_; }
        uint32_t TargetsHeight() const { return targetsHeight_; }

    private:
        uint32_t writeIndex_ = 0;
        uint32_t targetsWidth_ = 0;
        uint32_t targetsHeight_ = 0;
    };
}
