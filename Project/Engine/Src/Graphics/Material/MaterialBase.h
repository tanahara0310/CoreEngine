#pragma once

#include <d3d12.h>
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include <wrl.h>

namespace CoreEngine
{
    /// @brief マテリアル基底クラス（テンプレート）
    /// @details GPU定数バッファの確保・マップ・解放を共通化するユーティリティ基底。
    ///          多態的には使用しない（各マテリアル型は具象型として保持する）。
    /// @tparam TData シェーダー定数バッファに対応するデータ型
    template<typename TData>
    class MaterialBase {
    public:
        ~MaterialBase() {
            if (materialResource_ && materialData_) {
                materialResource_->Unmap(0, nullptr);
                materialData_ = nullptr;
            }
        }

        /// @brief GPU定数バッファの仮想アドレスを取得（コマンドリストへのバインド用）
        D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const {
            return materialResource_->GetGPUVirtualAddress();
        }

    protected:
        /// @brief GPUバッファを確保してマップします
        /// @param device D3D12デバイス
        void InitializeBuffer(ID3D12Device* device) {
            materialResource_ = ResourceFactory::CreateBufferResource(device, sizeof(TData));
            materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
        TData* materialData_ = nullptr;
    };

} // namespace CoreEngine
