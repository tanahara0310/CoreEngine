#pragma once
#include <d3d12.h>

namespace CoreEngine
{
    /// @brief GBuffer パスに対応するレンダラーのインターフェース
    /// @note GBuffer 描画が必要なレンダラーのみがこのインターフェースを実装する
    class IGBufferRenderer {
    public:
        virtual ~IGBufferRenderer() = default;

        /// @brief GBuffer パスの開始（GBuffer 用 RootSignature・PSO をバインド）
        /// @param cmdList コマンドリスト
        virtual void BeginGBufferPass(ID3D12GraphicsCommandList* cmdList) = 0;
    };
}
