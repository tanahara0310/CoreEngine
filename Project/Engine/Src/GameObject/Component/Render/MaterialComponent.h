#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine
{
class MeshRendererComponent;

/// @brief 兄弟の `MeshRendererComponent` が持つマテリアルをまとめて操作するコンポーネント。
/// @details 実体（`MaterialInstance`）は `Model` が持つため、ここは全スロットへ一括適用する
///          操作の側だけを担う。α<1 のときブレンドモードを自動でアルファブレンドへ切り替える。
class MaterialComponent : public IComponent {
public:
    const char* GetTypeName() const override { return "Material"; }

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "マテリアル"; }

    const char* GetInspectorIcon() const override { return "material.png"; }

    void GetInspectorIconColor(float* outRgba) const override
    {
        outRgba[0] = 0.90f; outRgba[1] = 0.30f; outRgba[2] = 0.40f; outRgba[3] = 1.0f;
    }

    /// @brief 色・PBR ファクター・各種フラグの編集 UI
    /// @return 値が変更されたら true
    bool DrawInspector() override;
#endif

    /// @brief 兄弟のメッシュ描画を捕まえ、遅延適用していた値を反映する
    void Start() override;

    // ===== 一括設定（全マテリアルスロットへ適用） =====

    /// @brief PBR ファクターを設定（テクスチャ有りマテリアルではマップ値と乗算される）
    void SetPBR(float metallic, float roughness, float occlusionStrength = 1.0f);

    /// @brief ベースカラーを設定（α<1 で自動的にアルファブレンドへ切り替わる）
    void SetColor(const Vector4& color);

    /// @brief 法線マップの有効/無効
    void SetNormalMapEnabled(bool enable);

    /// @brief IBL 強度（0 でこのモデルの IBL を無効化）
    void SetIBLIntensity(float intensity);

    /// @brief IBL の有効/無効（強度 1/0 の設定に相当）
    void SetIBLEnabled(bool enable) { SetIBLIntensity(enable ? 1.0f : 0.0f); }

    /// @brief ライティングの有効/無効
    void SetLightingEnabled(bool enable);

    // ===== 取得 =====

    /// @brief 代表マテリアル（スロット 0）。モデル未ロードなら nullptr
    MaterialInstance* GetMaterial() const;

private:
    /// @brief 全マテリアルスロットへ関数を適用する（未ロードなら false）
    bool ForEachMaterial(const std::function<void(MaterialInstance*)>& fn) const;

    /// @brief α<1 かつディザリング OFF ならアルファブレンドへ切り替える
    void UpdateBlendModeForAlpha() const;

    mutable MeshRendererComponent* renderer_ = nullptr;

    // Start() より前に呼ばれた設定を保持して後で適用する
    // （AddComponent 直後にシーンが値を入れる書き方を許すため）
    struct PendingPBR { float metallic; float roughness; float occlusion; };
    std::optional<PendingPBR> pendingPBR_;
    std::optional<Vector4> pendingColor_;
    std::optional<bool> pendingNormalMap_;
    std::optional<float> pendingIBL_;
    std::optional<bool> pendingLighting_;
};
}
