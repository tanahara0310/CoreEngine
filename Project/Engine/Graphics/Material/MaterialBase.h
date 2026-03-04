#pragma once

#include "IMaterial.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include <wrl.h>

namespace CoreEngine
{
    /// @brief マテリアル基底クラス（テンプレート）
    /// @details GPUバッファの確保・マップ・解放を共通化します。
    /// @tparam TData シェーダー定数バッファに対応するデータ型
    template<typename TData>
    class MaterialBase : public IMaterial {
    public:
        ~MaterialBase() override {
            if (materialResource_ && materialData_) {
                materialResource_->Unmap(0, nullptr);
                materialData_ = nullptr;
            }
        }

        D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const override {
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
