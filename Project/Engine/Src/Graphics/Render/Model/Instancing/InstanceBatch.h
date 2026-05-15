#pragma once
#include <d3d12.h>
#include <cstdint>
#include <vector>
#include <functional>

#include "Graphics/Model/TransformationMatrix.h"

namespace CoreEngine
{
    class ModelResource;

    /// @brief インスタンシング描画のバッチを識別するキー
    /// 同一キーの描画リクエストは 1 度の DrawIndexedInstanced にまとめられる。
    struct InstanceBatchKey {
        const ModelResource* resource = nullptr;  ///< 同一 VB / IB
        uint32_t subMeshIndex = 0;                ///< サブメッシュインデックス
        uint64_t baseColorSRV = 0;                ///< テクスチャハンドル
        uint64_t normalMapSRV = 0;
        uint64_t metallicRoughnessSRV = 0;
        uint64_t occlusionSRV = 0;
        uint64_t materialCBV = 0;                 ///< マテリアル定数バッファ
        bool isGBufferPass = false;               ///< Forward / GBuffer の区別
        ID3D12PipelineState* customForwardPSO = nullptr; ///< カスタムシェーダー PSO（nullptr = 既定）

        bool operator==(const InstanceBatchKey& other) const {
            return resource == other.resource
                && subMeshIndex == other.subMeshIndex
                && baseColorSRV == other.baseColorSRV
                && normalMapSRV == other.normalMapSRV
                && metallicRoughnessSRV == other.metallicRoughnessSRV
                && occlusionSRV == other.occlusionSRV
                && materialCBV == other.materialCBV
                && isGBufferPass == other.isGBufferPass
                && customForwardPSO == other.customForwardPSO;
        }
    };

    /// @brief InstanceBatchKey 用ハッシュ関数
    struct InstanceBatchKeyHash {
        size_t operator()(const InstanceBatchKey& k) const noexcept {
            auto mix = [](size_t a, size_t b) {
                return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
                };
            size_t h = std::hash<const void*>{}(k.resource);
            h = mix(h, std::hash<uint32_t>{}(k.subMeshIndex));
            h = mix(h, std::hash<uint64_t>{}(k.baseColorSRV));
            h = mix(h, std::hash<uint64_t>{}(k.normalMapSRV));
            h = mix(h, std::hash<uint64_t>{}(k.metallicRoughnessSRV));
            h = mix(h, std::hash<uint64_t>{}(k.occlusionSRV));
            h = mix(h, std::hash<uint64_t>{}(k.materialCBV));
            h = mix(h, std::hash<bool>{}(k.isGBufferPass));
            h = mix(h, std::hash<const void*>{}(k.customForwardPSO));
            return h;
        }
    };

    /// @brief 1 バッチの実体（同一キーに集約された行列リスト）
    struct InstanceBatch {
        InstanceBatchKey                    key;
        std::vector<TransformationMatrix>   instances;
        D3D12_GPU_VIRTUAL_ADDRESS           materialCBV = 0;  ///< バッチ共通のマテリアルCBV
    };
}
