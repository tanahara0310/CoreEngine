#pragma once

#include "MaterialBase.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Math/MathCore.h"

namespace CoreEngine
{
    /// @brief Sprite専用マテリアルのGPUデータ（シェーダーの cbuffer gMaterial とレイアウトを一致させること）
    struct SpriteMaterialData {
        Vector4 color;
        Matrix4x4 uvTransform;
        uint32_t filterMode;  ///< 0 = 線形 / 1 = ポイント
        Vector3 padding;
    };

    static constexpr Cb::Field kSpriteMaterialDataFields[] = {
        CB_FIELD(SpriteMaterialData, color), CB_FIELD(SpriteMaterialData, uvTransform),
        CB_FIELD(SpriteMaterialData, filterMode), CB_FIELD(SpriteMaterialData, padding),
    };
    CB_VERIFY_LAYOUT(SpriteMaterialData, kSpriteMaterialDataFields);

    /// @brief Sprite専用マテリアルクラス
    /// @details スプライト描画に必要なGPU定数バッファ（color + uvTransform + filterMode）を管理します。
    class SpriteMaterialInstance : public MaterialBase<SpriteMaterialData> {
    public:
        /// @brief GPU 定数バッファを確保して常時 Map する
        void Initialize(ID3D12Device* device);

        // ===== Color =====
        void SetColor(const Vector4& color);
        /// @brief 乗算カラー（RGBA）を取得
        Vector4 GetColor() const;

        // ===== UV Transform =====
        void SetUVTransform(const Matrix4x4& uvTransform);
        /// @brief UV トランスフォーム行列を取得
        Matrix4x4 GetUVTransform() const;

        // ===== Filter =====
        /// @brief サンプリングのしかた（0 = 線形 / 1 = ポイント）
        void SetFilterMode(uint32_t filterMode);
    };

} // namespace CoreEngine
