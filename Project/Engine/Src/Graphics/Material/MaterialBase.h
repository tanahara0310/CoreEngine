#pragma once

#include <d3d12.h>
#include "Graphics/RHI/Resource/ConstantBufferPool.h"

namespace CoreEngine
{
    /// @brief マテリアル基底クラス（テンプレート）
    /// @details GPU定数バッファの領域の確保・解放を共通化するユーティリティ基底。
    ///          領域は ConstantBufferPool から借り、破棄のときに返す。
    ///          多態的には使用しない（各マテリアル型は具象型として保持する）。
    /// @tparam TData シェーダー定数バッファに対応するデータ型
    template<typename TData>
    class MaterialBase {
    public:
        MaterialBase() = default;
        ~MaterialBase() {
            if (pool_) {
                pool_->Retire(block_);
            }
            materialData_ = nullptr;
        }

        MaterialBase(const MaterialBase&) = delete;
        MaterialBase& operator=(const MaterialBase&) = delete;

        /// @brief GPU定数バッファの仮想アドレスを取得（コマンドリストへのバインド用）
        D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const {
            return block_.gpuAddress;
        }

    protected:
        /// @brief 定数バッファの領域をプールから借りる
        /// @param pool 借りる先（GraphicsCore::GetConstantBufferPool）
        void InitializeBuffer(ConstantBufferPool& pool) {
            if (pool_) {
                pool_->Retire(block_);
            }
            pool_ = &pool;
            block_ = pool.Allocate(static_cast<uint32_t>(sizeof(TData)));
            materialData_ = static_cast<TData*>(block_.cpu);
        }

        ConstantBufferPool* pool_ = nullptr;
        ConstantBufferBlock block_{};
        TData* materialData_ = nullptr;
    };

} // namespace CoreEngine
