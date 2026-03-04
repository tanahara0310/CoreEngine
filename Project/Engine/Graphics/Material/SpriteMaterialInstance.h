#pragma once

#include "MaterialBase.h"
#include "MathCore.h"

namespace CoreEngine
{
    /// @brief Sprite専用マテリアルのGPUデータ（シェーダーの cbuffer gMaterial とレイアウトを一致させること）
    struct SpriteMaterialData {
        Vector4 color;
        Matrix4x4 uvTransform;
    };

    /// @brief Sprite専用マテリアルクラス
    /// @details スプライト描画に必要なGPU定数バッファ（color + uvTransform）を管理します。
    class SpriteMaterialInstance : public MaterialBase<SpriteMaterialData> {
    public:
        void Initialize(ID3D12Device* device) override;

        // ===== Color =====
        void SetColor(const Vector4& color) override;
        Vector4 GetColor() const override;

        // ===== UV Transform =====
        void SetUVTransform(const Matrix4x4& uvTransform);
        Matrix4x4 GetUVTransform() const;
    };

} // namespace CoreEngine
