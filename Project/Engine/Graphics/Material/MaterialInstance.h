#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "Engine/Graphics/Material/IMaterial.h"
#include "MathCore.h"
#include "Structs/MaterialConstants.h"

namespace CoreEngine
{
    /// @brief 1つのマテリアルのGPU定数バッファを保持するクラス
    /// @details モデルインスタンスごとに1つ生成し、GPU側の定数バッファを管理します。
    class MaterialInstance : public IMaterial {
    public:
        /// @brief 初期化
        /// @param device デバイス
        void Initialize(ID3D12Device* device);

        // ===== Color =====
        void SetColor(const Vector4& color) { materialData_->color = color; }
        Vector4 GetColor() const { return materialData_->color; }

        // ===== Lighting =====
        void SetLightingEnabled(bool enable) { materialData_->enableLighting = enable ? 1 : 0; }
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
        void SetDitheringEnabled(bool enable) { materialData_->enableDithering = enable ? 1 : 0; }
        bool IsDitheringEnabled() const { return materialData_->enableDithering != 0; }
        void SetDitheringScale(float scale) { materialData_->ditheringScale = scale; }
        float GetDitheringScale() const { return materialData_->ditheringScale; }

        // ===== Environment Map =====
        void SetEnvironmentMapEnabled(bool enable) { materialData_->enableEnvironmentMap = enable ? 1 : 0; }
        bool IsEnvironmentMapEnabled() const { return materialData_->enableEnvironmentMap != 0; }
        void SetEnvironmentMapIntensity(float intensity) { materialData_->environmentMapIntensity = intensity; }
        float GetEnvironmentMapIntensity() const { return materialData_->environmentMapIntensity; }
        void SetEnvironmentRotationY(float rotationY) { materialData_->environmentRotationY = rotationY; }
        float GetEnvironmentRotationY() const { return materialData_->environmentRotationY; }

        // ===== PBR =====
        void SetPBREnabled(bool enable) { materialData_->enablePBR = enable ? 1 : 0; }
        bool IsPBREnabled() const { return materialData_->enablePBR != 0; }
        void SetMetallic(float metallic) { materialData_->metallic = metallic; }
        float GetMetallic() const { return materialData_->metallic; }
        void SetRoughness(float roughness) { materialData_->roughness = roughness; }
        float GetRoughness() const { return materialData_->roughness; }
        void SetAO(float ao) { materialData_->ao = ao; }
        float GetAO() const { return materialData_->ao; }

        // ===== PBR Texture Maps =====
        void SetNormalMapEnabled(bool enable) { materialData_->useNormalMap = enable ? 1 : 0; }
        bool IsNormalMapEnabled() const { return materialData_->useNormalMap != 0; }
        void SetMetallicMapEnabled(bool enable) { materialData_->useMetallicMap = enable ? 1 : 0; }
        bool IsMetallicMapEnabled() const { return materialData_->useMetallicMap != 0; }
        void SetRoughnessMapEnabled(bool enable) { materialData_->useRoughnessMap = enable ? 1 : 0; }
        bool IsRoughnessMapEnabled() const { return materialData_->useRoughnessMap != 0; }
        void SetAOMapEnabled(bool enable) { materialData_->useAOMap = enable ? 1 : 0; }
        bool IsAOMapEnabled() const { return materialData_->useAOMap != 0; }

        // ===== IBL =====
        void SetIBLEnabled(bool enable) { materialData_->enableIBL = enable ? 1 : 0; }
        bool IsIBLEnabled() const { return materialData_->enableIBL != 0; }
        void SetIBLIntensity(float intensity) { materialData_->iblIntensity = intensity; }
        float GetIBLIntensity() const { return materialData_->iblIntensity; }

        // ===== GPU Access =====
        /// @brief GPU仮想アドレスを取得
        D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const
        {
            return materialResource_->GetGPUVirtualAddress();
        }

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr;
        MaterialConstants* materialData_ = nullptr;
    };

} // namespace CoreEngine
