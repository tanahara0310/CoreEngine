#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
class MeshRendererComponent;

/// @brief 兄弟の `MeshRendererComponent` が持つマテリアルをまとめて操作するコンポーネント。
/// @details 実体（`MaterialInstance`）は `Model` が持つため、ここは全スロットへ一括適用する
///          操作の側だけを担う。α<1 のときブレンドモードを自動でアルファブレンドへ切り替える。
class MaterialComponent : public IComponent {
public:
    const char* GetTypeName() const override { return "Material"; }

    // 値の実体は MaterialInstance 側にあり、このクラスはメンバとして持たない。
    // だから記述子はメンバ式ではなく getter / setter で組み立てる
    REFLECT_BEGIN(MaterialComponent, "マテリアル")
        REFLECT_ACCESSOR("color", "ベースカラー", GetColor, SetColor,
            p.type = ::CoreEngine::Reflection::PropertyType::Color)
        REFLECT_ACCESSOR("metallic", "メタリック", GetMetallic, SetMetallic,
            p.range = Range(0.0f, 1.0f))
        REFLECT_ACCESSOR("roughness", "ラフネス", GetRoughness, SetRoughness,
            p.range = Range(0.0f, 1.0f))
        REFLECT_ACCESSOR("occlusionStrength", "オクルージョン", GetOcclusionStrength,
            SetOcclusionStrength, p.range = Range(0.0f, 1.0f))
        REFLECT_ACCESSOR("iblIntensity", "IBL 強度", GetIBLIntensity, SetIBLIntensity,
            p.range = Range(0.0f, 2.0f))
        REFLECT_ACCESSOR("lighting", "ライティング", IsLightingEnabled, SetLightingEnabled)
        REFLECT_ACCESSOR("normalMap", "法線マップ", IsNormalMapEnabled, SetNormalMapEnabled)
    REFLECT_END()

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "マテリアル"; }

    /// @brief 色・PBR ファクター・各種フラグの編集 UI
    /// @return 値が変更されたら true
    bool DrawInspector() override;

    /// @brief メッシュ待ちで実体が無いことを添える
    void DrawInspectorExtra() override;
#endif

    /// @brief 兄弟のメッシュ描画を捕まえ、遅延適用していた値を反映する
    void Start() override;

    /// @brief 兄弟のメッシュ描画を使う
    bool RequiresComponent(const IComponent& other) const override;

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

    /// @brief PBR ファクターを 1 つずつ設定する（記述子から使う）
    void SetMetallic(float value);
    void SetRoughness(float value);
    void SetOcclusionStrength(float value);

    // ===== 取得 =====

    /// @brief 代表マテリアル（スロット 0）。モデル未ロードなら nullptr
    MaterialInstance* GetMaterial() const;

    // 実体があればそこから読み、メッシュ待ちの間は Start で反映する控えを返す。
    // 控えも無い場合はエンジン既定値（MaterialInstance の初期値と同じ）
    Vector4 GetColor() const;
    float GetMetallic() const;
    float GetRoughness() const;
    float GetOcclusionStrength() const;
    float GetIBLIntensity() const;
    bool IsLightingEnabled() const;
    bool IsNormalMapEnabled() const;

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
