#pragma once

#include "MaterialBase.h"
#include "Math/MathCore.h"
#include "MaterialConstants.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"

namespace CoreEngine
{
    /// @brief 1つのマテリアルのGPU定数バッファを保持するクラス（PBR専用）
    /// @note glTF 準拠の「ファクター × テクスチャ」乗算方式。
    ///       Metallic / Roughness / EmissiveFactor はテクスチャと乗算されるファクター値。
    /// @note IBL の有効/無効はシーン側で決まる。iblIntensity=0 で個別オプトアウト。
    class MaterialInstance : public MaterialBase<MaterialConstants> {
    public:
        void Initialize(ID3D12Device* device);

        // ===== Base Color =====
        /// @brief ベースカラーファクターを設定（ベースカラーテクスチャと乗算される）
        void SetColor(const Vector4& color) { materialData_->color = color; }
        Vector4 GetColor() const { return materialData_->color; }

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

        // ===== IBL =====
        /// @brief IBL強度を設定（0 でこのマテリアルの IBL を無効化。シーンに IBL マップが無い場合は常に無効）
        void SetIBLIntensity(float intensity) { materialData_->iblIntensity = intensity; }
        float GetIBLIntensity() const      { return materialData_->iblIntensity; }

        // ===== Serialization =====
        /// @brief マテリアルパラメータを JSON に書き出す
        nlohmann::json ToJson() const;

        /// @brief JSON からマテリアルパラメータを復元する
        /// @note 旧フォーマットのキー（"ao" / "ibl" 等）も読み替えて互換を維持する
        void FromJson(const nlohmann::json& j);
    };

} // namespace CoreEngine
