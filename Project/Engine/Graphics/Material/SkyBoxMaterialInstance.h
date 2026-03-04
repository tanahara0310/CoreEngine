#pragma once

#include "MaterialBase.h"

namespace CoreEngine
{
    /// @brief SkyBox専用マテリアルのGPUデータ（シェーダーとレイアウトを一致させること）
    struct SkyBoxGpuData {
        Vector4 color;
    };

    /// @brief SkyBox専用マテリアルクラス
    /// @details SkyBox描画に必要な最小限のGPU定数バッファを管理します。
    class SkyBoxMaterialInstance : public MaterialBase<SkyBoxGpuData> {
    public:
        void Initialize(ID3D12Device* device) override;
        void SetColor(const Vector4& color) override;
        Vector4 GetColor() const override;
    };

} // namespace CoreEngine
