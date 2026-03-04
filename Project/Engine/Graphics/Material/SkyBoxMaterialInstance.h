#pragma once

#include "IMaterial.h"
#include <d3d12.h>
#include <wrl.h>
#include "Engine/Graphics/Resource/ResourceFactory.h"

namespace CoreEngine
{
    /// @brief SkyBox専用マテリアルクラス
    /// @details SkyBox描画に必要な最小限のGPU定数バッファを管理します。
    class SkyBoxMaterialInstance : public IMaterial {
    public:
        /// @brief 初期化
        /// @param device D3D12デバイス
        void Initialize(ID3D12Device* device);

        void SetColor(const Vector4& color) override;
        Vector4 GetColor() const override;
        D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const override;

    private:
        /// @brief GPUに送信するデータ（シェーダーとレイアウトを一致させること）
        struct GpuData {
            Vector4 color;
        };

        Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr;
        GpuData* materialData_ = nullptr;
    };

} // namespace CoreEngine
