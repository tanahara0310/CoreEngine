#pragma once
#include <d3d12.h>

namespace CoreEngine
{
    /// @brief シャドウマップ描画（深度のみ）に必要な最小限の GPU リソースをまとめたデータ構造
    /// Model が組み立て、ShadowMapRenderer::BindShadowDrawPacket() が消費してコマンドを発行する。
    /// これにより Model 側から D3D12 コマンド発行とルートパラメータ解決の責任を除去する
    /// （ModelDrawPacket / BaseModelRenderer::BindModelDrawPacket と同じ設計）。
    struct ShadowDrawPacket {
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
        D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
        UINT indexCount = 0;

        // 定数バッファ (CBV: gLightTransform = World + LightViewProjection)
        D3D12_GPU_VIRTUAL_ADDRESS transformCBV = 0;
    };
}
