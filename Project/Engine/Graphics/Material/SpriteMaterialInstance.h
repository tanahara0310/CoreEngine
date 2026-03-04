#pragma once

#include "IMaterial.h"
#include <d3d12.h>
#include <wrl.h>
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "MathCore.h"

namespace CoreEngine
{
    /// @brief Sprite専用マテリアルクラス
    /// @details スプライト描画に必要なGPU定数バッファ（color + uvTransform）を管理します。
    class SpriteMaterialInstance : public IMaterial {
    public:
        /// @brief 初期化
        /// @param device D3D12デバイス
        void Initialize(ID3D12Device* device);

        // ===== Color =====
        void SetColor(const Vector4& color) override;
        Vector4 GetColor() const override;

        // ===== UV Transform =====
        void SetUVTransform(const Matrix4x4& uvTransform);
        Matrix4x4 GetUVTransform() const;

        // ===== GPU Access =====
        D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const override;

    private:
        /// @brief GPUに送信するデータ（シェーダーの cbuffer gMaterial とレイアウトを一致させること）
        struct GpuData {
            Vector4 color;
            Matrix4x4 uvTransform;
        };

        Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr;
        GpuData* materialData_ = nullptr;
    };

} // namespace CoreEngine
