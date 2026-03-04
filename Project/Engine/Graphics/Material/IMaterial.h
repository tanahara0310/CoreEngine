#pragma once

#include <d3d12.h>
#include "Vector/Vector4.h"

namespace CoreEngine
{
    /// @brief マテリアルの共通インターフェース
    /// @details 全マテリアル型が実装すべき最小契約を定義します。
    /// @note 新しいマテリアル型を追加する場合はこのインターフェースを継承してください。
    class IMaterial {
    public:
        virtual ~IMaterial() = default;

        /// @brief GPUバッファを初期化します
        /// @param device D3D12デバイス
        virtual void Initialize(ID3D12Device* device) = 0;

        /// @brief カラーを設定
        virtual void SetColor(const Vector4& color) = 0;

        /// @brief カラーを取得
        virtual Vector4 GetColor() const = 0;

        /// @brief GPU定数バッファの仮想アドレスを取得（コマンドリストへのバインド用）
        virtual D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const = 0;
    };

} // namespace CoreEngine
