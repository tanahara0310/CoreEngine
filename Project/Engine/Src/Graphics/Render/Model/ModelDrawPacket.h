#pragma once
#include <d3d12.h>
#include <array>

namespace CoreEngine
{
    /// @brief 1 サブメッシュの描画に必要な GPU リソースをまとめたデータ構造
    /// Model が組み立て、BaseModelRenderer::BindModelDrawPacket() が消費してコマンドを発行する。
    /// これにより Model 側から D3D12 コマンド発行とルートパラメータ解決の責任を除去する。
    struct ModelDrawPacket {
        // 頂点/インデックスバッファ
        std::array<D3D12_VERTEX_BUFFER_VIEW, 2> vertexBufferViews = {};
        UINT vertexBufferViewCount = 1; ///< 通常モデル=1, スキニングモデル=2
        D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
        UINT indexCount = 0;
        UINT startIndex = 0;
        UINT instanceCount = 1; ///< インスタンシング描画のインスタンス数（通常モデル: バッチサイズ / スキニング: 1）

        // インスタンスデータ (Root SRV: StructuredBuffer<TransformationMatrix>)
        // 通常モデル: バッチ集約された行列配列の先頭アドレス
        // スキニング: 単一インスタンスの行列バッファ先頭アドレス
        D3D12_GPU_VIRTUAL_ADDRESS instanceDataSRV = 0;

        // 定数バッファ (CBV)
        D3D12_GPU_VIRTUAL_ADDRESS materialCBV = 0;

        // テクスチャ (SRV) — ptr == 0 のものはバインドをスキップ
        D3D12_GPU_DESCRIPTOR_HANDLE baseColorSRV = {};
        D3D12_GPU_DESCRIPTOR_HANDLE normalMapSRV = {};
        D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessSRV = {};
        D3D12_GPU_DESCRIPTOR_HANDLE occlusionSRV = {};
        D3D12_GPU_DESCRIPTOR_HANDLE emissiveSRV = {};

        // スキニング専用 (isSkinned == false のとき matrixPaletteSRV は未使用)
        bool isSkinned = false;
        D3D12_GPU_DESCRIPTOR_HANDLE matrixPaletteSRV = {};
    };
}
