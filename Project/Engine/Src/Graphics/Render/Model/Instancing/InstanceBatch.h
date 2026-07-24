#pragma once
#include <d3d12.h>
#include <cstdint>
#include <vector>
#include <functional>
#include <tuple>
#include <type_traits>

#include "Graphics/Model/TransformationMatrix.h"

namespace CoreEngine
{
    class CustomShaderPipeline;
    class ModelResource;
    class ICustomShaderProvider;

    /// @brief インスタンシング描画のバッチを識別するキー
    /// 同一キーの描画リクエストは 1 度の DrawIndexedInstanced にまとめられる。
    struct InstanceBatchKey {
        const ModelResource* resource = nullptr;  ///< 同一 VB / IB
        uint32_t subMeshIndex = 0;                ///< サブメッシュインデックス
        uint32_t lodIndex = 0;                    ///< LODレベル（0=フル詳細）
        uint64_t baseColorSRV = 0;                ///< テクスチャハンドル
        uint64_t normalMapSRV = 0;
        uint64_t metallicRoughnessSRV = 0;
        uint64_t occlusionSRV = 0;
        uint64_t emissiveSRV = 0;
        uint64_t materialCBV = 0;                 ///< マテリアル定数バッファ
        bool isGBufferPass = false;               ///< Forward / GBuffer の区別
        ID3D12PipelineState* customForwardPSO = nullptr; ///< カスタムシェーダー PSO（nullptr = 既定）
        ID3D12RootSignature* customRootSignature = nullptr; ///< カスタムシェーダー RootSignature（nullptr = 既定）
        const ICustomShaderProvider* customProvider = nullptr; ///< カスタムリソースバインドコールバック（nullptr = なし）
        const CustomShaderPipeline* customPipeline = nullptr; ///< BindCustomResources に渡すパイプライン情報

        /// @brief キーを組み立てる（生成箇所を一本化するファクトリ）
        /// @details 「GBuffer パスではカスタムシェーダーを無効化する」規則をここに集約する。
        ///          テクスチャは Model が解決済みのハンドルを渡す（baseColor はオーバーライド解決後）。
        static InstanceBatchKey Make(
            const ModelResource* resource, uint32_t subMeshIndex, uint32_t lodIndex,
            D3D12_GPU_DESCRIPTOR_HANDLE baseColor, D3D12_GPU_DESCRIPTOR_HANDLE normalMap,
            D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughness, D3D12_GPU_DESCRIPTOR_HANDLE occlusion,
            D3D12_GPU_DESCRIPTOR_HANDLE emissive,
            D3D12_GPU_VIRTUAL_ADDRESS materialCBV, bool isGBufferPass,
            ID3D12PipelineState* customForwardPSO, ID3D12RootSignature* customRootSignature,
            const ICustomShaderProvider* customProvider, const CustomShaderPipeline* customPipeline)
        {
            InstanceBatchKey key{};
            key.resource = resource;
            key.subMeshIndex = subMeshIndex;
            key.lodIndex = lodIndex;
            key.baseColorSRV = baseColor.ptr;
            key.normalMapSRV = normalMap.ptr;
            key.metallicRoughnessSRV = metallicRoughness.ptr;
            key.occlusionSRV = occlusion.ptr;
            key.emissiveSRV = emissive.ptr;
            key.materialCBV = static_cast<uint64_t>(materialCBV);
            key.isGBufferPass = isGBufferPass;
            // カスタムシェーダーは Forward 描画専用（GBuffer パスは既定シェーダーで描く）
            key.customForwardPSO = isGBufferPass ? nullptr : customForwardPSO;
            key.customRootSignature = isGBufferPass ? nullptr : customRootSignature;
            key.customProvider = isGBufferPass ? nullptr : customProvider;
            key.customPipeline = isGBufferPass ? nullptr : customPipeline;
            return key;
        }

        /// @brief 全フィールドの参照 tuple（==・ハッシュの単一同期点）
        /// @note フィールドを追加したらここへ足すだけで比較・ハッシュの両方に反映される
        auto Fields() const {
            return std::tie(resource, subMeshIndex, lodIndex,
                baseColorSRV, normalMapSRV, metallicRoughnessSRV, occlusionSRV, emissiveSRV,
                materialCBV, isGBufferPass,
                customForwardPSO, customRootSignature, customProvider, customPipeline);
        }

        bool operator==(const InstanceBatchKey& other) const {
            return Fields() == other.Fields();
        }
    };

    /// @brief InstanceBatchKey 用ハッシュ関数（Fields() の全要素を順に混合）
    struct InstanceBatchKeyHash {
        size_t operator()(const InstanceBatchKey& k) const noexcept {
            size_t h = 0;
            std::apply([&h](const auto&... field) {
                auto mix = [&h](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    h ^= std::hash<T>{}(value) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
                    };
                (mix(field), ...);
                }, k.Fields());
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
