#pragma once

#include "MaterialBase.h"
#include "Math/MathCore.h"
#include "MaterialConstants.h"

namespace CoreEngine
{
    /// @brief 1つのマテリアルのGPU定数バッファを保持するクラス（PBR専用）
    class MaterialInstance : public MaterialBase<MaterialConstants> {
    public:
        void Initialize(ID3D12Device* device) override;

        // ===== Color =====
        void SetColor(const Vector4& color) override { materialData_->color = color; }
        Vector4 GetColor() const override { return materialData_->color; }

        // ===== Lighting =====
        /// @brief ライティング有効/無効 (false=アンリット, true=PBR)
        void SetLightingEnabled(bool enable) { materialData_->enableLighting = static_cast<int32_t>(enable); }
        bool IsLightingEnabled() const { return materialData_->enableLighting != 0; }

        // ===== UV Transform =====
        void SetUVTransform(const Matrix4x4& uvTransform) { materialData_->uvTransform = uvTransform; }
        Matrix4x4 GetUVTransform() const { return materialData_->uvTransform; }

        // ===== PBR Parameters =====
        void SetMetallic(float metallic)   { materialData_->metallic = metallic; }
        float GetMetallic() const          { return materialData_->metallic; }
        void SetRoughness(float roughness) { materialData_->roughness = roughness; }
        float GetRoughness() const         { return materialData_->roughness; }
        void SetAO(float ao)               { materialData_->ao = ao; }
        float GetAO() const                { return materialData_->ao; }

        // ===== PBR Texture Maps =====
        void SetNormalMapEnabled(bool enable)    { materialData_->useNormalMap    = static_cast<int32_t>(enable); }
        bool IsNormalMapEnabled() const          { return materialData_->useNormalMap != 0; }
        void SetMetallicMapEnabled(bool enable)  { materialData_->useMetallicMap  = static_cast<int32_t>(enable); }
        bool IsMetallicMapEnabled() const        { return materialData_->useMetallicMap != 0; }
        void SetRoughnessMapEnabled(bool enable) { materialData_->useRoughnessMap = static_cast<int32_t>(enable); }
        bool IsRoughnessMapEnabled() const       { return materialData_->useRoughnessMap != 0; }
        void SetAOMapEnabled(bool enable)        { materialData_->useAOMap        = static_cast<int32_t>(enable); }
        bool IsAOMapEnabled() const              { return materialData_->useAOMap != 0; }

        // ===== Alpha Dithering =====
        void SetDitheringEnabled(bool enable) { materialData_->enableDithering = static_cast<int32_t>(enable); }
        bool IsDitheringEnabled() const       { return materialData_->enableDithering != 0; }
        void SetDitheringScale(float scale)   { materialData_->ditheringScale = scale; }
        float GetDitheringScale() const       { return materialData_->ditheringScale; }

        // ===== IBL =====
        void SetIBLEnabled(bool enable)    { materialData_->enableIBL = static_cast<int32_t>(enable); }
        bool IsIBLEnabled() const          { return materialData_->enableIBL != 0; }
        void SetIBLIntensity(float intensity) { materialData_->iblIntensity = intensity; }
        float GetIBLIntensity() const      { return materialData_->iblIntensity; }
    };

} // namespace CoreEngine
