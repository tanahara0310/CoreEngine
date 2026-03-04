#pragma once

#include "MaterialBase.h"
#include "Math/MathCore.h"
#include "MaterialConstants.h"

namespace CoreEngine
{
    /// @brief 1つのマテリアルのGPU定数バッファを保持するクラス
    /// @details モデルインスタンスごとに1つ生成し、GPU側の定数バッファを管理します。
    class MaterialInstance : public MaterialBase<MaterialConstants> {
    public:
        void Initialize(ID3D12Device* device) override;

        // ===== Color =====
        void SetColor(const Vector4& color) override { materialData_->color = color; }
        Vector4 GetColor() const override { return materialData_->color; }

        // ===== Lighting =====
        void SetLightingEnabled(bool enable) { materialData_->enableLighting = static_cast<int32_t>(enable); }
        bool IsLightingEnabled() const { return materialData_->enableLighting != 0; }

        // ===== UV Transform =====
        void SetUVTransform(const Matrix4x4& uvTransform) { materialData_->uvTransform = uvTransform; }
        Matrix4x4 GetUVTransform() const { return materialData_->uvTransform; }

        // ===== Shininess =====
        void SetShininess(float shininess) { materialData_->shininess = shininess; }
        float GetShininess() const { return materialData_->shininess; }

        // ===== Shading Mode =====
        void SetShadingMode(ShadingMode mode) { materialData_->shadingMode = static_cast<int32_t>(mode); }
        ShadingMode GetShadingMode() const { return static_cast<ShadingMode>(materialData_->shadingMode); }

        // ===== Toon =====
        void SetToonThreshold(float threshold) { materialData_->toonThreshold = threshold; }
        float GetToonThreshold() const { return materialData_->toonThreshold; }
        void SetToonSmoothness(float smoothness) { materialData_->toonSmoothness = smoothness; }
        float GetToonSmoothness() const { return materialData_->toonSmoothness; }

        // ===== Dithering =====
        void SetDitheringEnabled(bool enable) { materialData_->enableDithering = static_cast<int32_t>(enable); }
        bool IsDitheringEnabled() const { return materialData_->enableDithering != 0; }
        void SetDitheringScale(float scale) { materialData_->ditheringScale = scale; }
        float GetDitheringScale() const { return materialData_->ditheringScale; }

        // ===== Environment Map =====
        void SetEnvironmentMapEnabled(bool enable) { materialData_->enableEnvironmentMap = static_cast<int32_t>(enable); }
        bool IsEnvironmentMapEnabled() const { return materialData_->enableEnvironmentMap != 0; }
        void SetEnvironmentMapIntensity(float intensity) { materialData_->environmentMapIntensity = intensity; }
        float GetEnvironmentMapIntensity() const { return materialData_->environmentMapIntensity; }
        void SetEnvironmentRotationY(float rotationY) { materialData_->environmentRotationY = rotationY; }
        float GetEnvironmentRotationY() const { return materialData_->environmentRotationY; }

        // ===== PBR =====
        void SetPBREnabled(bool enable) { materialData_->enablePBR = static_cast<int32_t>(enable); }
        bool IsPBREnabled() const { return materialData_->enablePBR != 0; }
        void SetMetallic(float metallic) { materialData_->metallic = metallic; }
        float GetMetallic() const { return materialData_->metallic; }
        void SetRoughness(float roughness) { materialData_->roughness = roughness; }
        float GetRoughness() const { return materialData_->roughness; }
        void SetAO(float ao) { materialData_->ao = ao; }
        float GetAO() const { return materialData_->ao; }

        // ===== PBR Texture Maps =====
        void SetNormalMapEnabled(bool enable) { materialData_->useNormalMap = static_cast<int32_t>(enable); }
        bool IsNormalMapEnabled() const { return materialData_->useNormalMap != 0; }
        void SetMetallicMapEnabled(bool enable) { materialData_->useMetallicMap = static_cast<int32_t>(enable); }
        bool IsMetallicMapEnabled() const { return materialData_->useMetallicMap != 0; }
        void SetRoughnessMapEnabled(bool enable) { materialData_->useRoughnessMap = static_cast<int32_t>(enable); }
        bool IsRoughnessMapEnabled() const { return materialData_->useRoughnessMap != 0; }
        void SetAOMapEnabled(bool enable) { materialData_->useAOMap = static_cast<int32_t>(enable); }
        bool IsAOMapEnabled() const { return materialData_->useAOMap != 0; }

        // ===== IBL =====
        void SetIBLEnabled(bool enable) { materialData_->enableIBL = static_cast<int32_t>(enable); }
        bool IsIBLEnabled() const { return materialData_->enableIBL != 0; }
        void SetIBLIntensity(float intensity) { materialData_->iblIntensity = intensity; }
        float GetIBLIntensity() const { return materialData_->iblIntensity; }
    };

} // namespace CoreEngine
