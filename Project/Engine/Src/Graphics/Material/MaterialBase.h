#pragma once

#include <cstring>
#include <type_traits>
#include <d3d12.h>
#include "Graphics/RHI/Resource/ConstantBufferPool.h"

namespace CoreEngine
{
    /// @brief マテリアル基底クラス（テンプレート）
    /// @details GPU定数バッファの領域の確保・解放を共通化するユーティリティ基底。
    ///          領域は ConstantBufferPool から借り、破棄のときに返す。
    ///          値は CPU 側の写し（materialData_）で読み書きし、書き換えたら Upload() で領域へ書き出す。
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
            gpuData_ = nullptr;
        }

        MaterialBase(const MaterialBase&) = delete;
        MaterialBase& operator=(const MaterialBase&) = delete;

        /// @brief GPU定数バッファの仮想アドレスを取得（コマンドリストへのバインド用）
        D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const {
            return block_.gpuAddress;
        }

    protected:
        /// @brief 定数バッファの領域をプールから借り、今の値を書き出す
        /// @param pool 借りる先（GraphicsCore::GetConstantBufferPool）
        void InitializeBuffer(ConstantBufferPool& pool) {
            if (pool_) {
                pool_->Retire(block_);
            }
            pool_ = &pool;
            block_ = pool.Allocate(static_cast<uint32_t>(sizeof(TData)));
            gpuData_ = static_cast<TData*>(block_.cpu);
            Upload();
        }

        /// @brief 値の写しを定数バッファの領域へ書き出す
        void Upload() {
            static_assert(std::is_trivially_copyable_v<TData>, "定数バッファのデータはバイト列のまま写せる型にすること");
            if (gpuData_) {
                std::memcpy(gpuData_, &materialData_, sizeof(TData));
            }
        }

        ConstantBufferPool* pool_ = nullptr;
        ConstantBufferBlock block_{};
        TData materialData_{};      ///< 値の写し（読み書きはこちら）
        TData* gpuData_ = nullptr;  ///< 定数バッファの領域（書き出すだけで読まない）
    };

} // namespace CoreEngine
