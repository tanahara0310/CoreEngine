#pragma once

#include "MaterialBase.h"
#include "Math/MathCore.h"
#include "MaterialConstants.h"

namespace CoreEngine
{
    /// @brief 1つのマテリアルのGPU定数バッファを保持するクラス（PBR専用）
    /// @note glTF 準拠の「ファクター × テクスチャ」乗算方式。
    ///       Metallic / Roughness / EmissiveFactor はテクスチャと乗算されるファクター値。
    class MaterialInstance : public MaterialBase<MaterialConstants> {
    public:
        void Initialize(ID3D12Device* device) override;

        // ===== Base Color =====
        /// @brief ベースカラーファクターを設定（ベースカラーテクスチャと乗算される）
        void SetColor(const Vector4& color) override { materialData_->color = color; }
        Vector4 GetColor() const override { return materialData_->color; }

        // ===== Lighting =====
        /// @brief ライティング有効/無効 (false=アンリット, true=PBR)
        void SetLightingEnabled(bool enable) { materialData_->enableLighting = static_cast<int32_t>(enable); }
        bool IsLightingEnabled() const { return materialData_->enableLighting != 0; }

        // ===== UV Transform =====
        void SetUVTransform(const Matrix4x4& uvTransform) { materialData_->uvTransform = uvTransform; }
        Matrix4x4 GetUVTransform() const { return materialData_->uvTransform; }

        // ===== PBR Factors =====
        /// @brief 金属性ファクター（MRテクスチャのBチャネルと乗算）
        void SetMetallic(float metallic)   { materialData_->metallic = metallic; }
        float GetMetallic() const          { return materialData_->metallic; }
        /// @brief 粗さファクター（MRテクスチャのGチャネルと乗算）
        void SetRoughness(float roughness) { materialData_->roughness = roughness; }
        float GetRoughness() const         { return materialData_->roughness; }
        /// @brief AOマップ適用強度 (0=AOマップ無効, 1=フル適用)
        void SetOcclusionStrength(float strength) { materialData_->occlusionStrength = strength; }
        float GetOcclusionStrength() const        { return materialData_->occlusionStrength; }
        /// @brief エミッシブファクター（エミッシブテクスチャと乗算）
        void SetEmissiveFactor(const Vector3& factor) { materialData_->emissiveFactor = factor; }
        Vector3 GetEmissiveFactor() const             { return materialData_->emissiveFactor; }

        // ===== Normal Map =====
        /// @brief 法線マップ使用フラグ（法線は乗算合成できないためフラグで制御）
        void SetNormalMapEnabled(bool enable)    { materialData_->useNormalMap = static_cast<int32_t>(enable); }
        bool IsNormalMapEnabled() const          { return materialData_->useNormalMap != 0; }

        // ===== Alpha Dithering =====
        void SetDitheringEnabled(bool enable) { materialData_->enableDithering = static_cast<int32_t>(enable); }
        bool IsDitheringEnabled() const       { return materialData_->enableDithering != 0; }
        void SetDitheringScale(float scale)   { materialData_->ditheringScale = scale; }
        float GetDitheringScale() const       { return materialData_->ditheringScale; }

        // ===== Alpha Cutoff (discard しきい値) =====
        void SetAlphaCutoff(float cutoff)     { materialData_->alphaCutoff = cutoff; }
        float GetAlphaCutoff() const          { return materialData_->alphaCutoff; }

        // ===== IBL / Shading Mode =====
        /// @brief シェーディングモードを設定
        void SetShadingMode(ShadingMode mode) { materialData_->shadingMode = static_cast<int32_t>(mode); }
        ShadingMode GetShadingMode() const     { return static_cast<ShadingMode>(materialData_->shadingMode); }

        /// @brief IBL 有効/無効（後方互換ラッパー）
        /// @details true → ShadingMode::PBR_IBL, false → ShadingMode::PBR
        void SetIBLEnabled(bool enable) {
            SetShadingMode(enable ? ShadingMode::PBR_IBL : ShadingMode::PBR);
        }
        bool IsIBLEnabled() const { return GetShadingMode() == ShadingMode::PBR_IBL; }

        void SetIBLIntensity(float intensity) { materialData_->iblIntensity = intensity; }
        float GetIBLIntensity() const      { return materialData_->iblIntensity; }
    };

} // namespace CoreEngine
