#pragma once

#include "GameObject/Model/ModelGameObject.h"
#include "Graphics/Material/MaterialConstants.h"

/// @brief 汎用モデルオブジェクト（glTFなどのモデル読み込み用）
class ModelObject : public CoreEngine::ModelGameObject {
public:
    /// @brief コンストラクタ
    /// @param modelPath ロードするモデルファイル名（AddObject 時に Initialize() が自動呼出しされる）
    explicit ModelObject(const std::string& modelPath = "") : modelPath_(modelPath) {}

    /// @brief ブレンドモードを取得
    /// @details α<1 かつディザリングOFF の場合は自動でアルファブレンドに切り替える
    CoreEngine::BlendMode GetBlendMode() const override;

    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "Model"; }

    /// @brief マテリアルインスタンスを直接取得（パラメータ直接操作用）
    /// @return MaterialInstance へのポインタ（未初期化の場合 nullptr）
    CoreEngine::MaterialInstance* GetMaterial();

    /// @brief PBRファクターを設定（テクスチャ有りマテリアルではマップ値と乗算される）
    /// @param metallic 金属性ファクター
    /// @param roughness 粗さファクター
    /// @param occlusionStrength AOマップ適用強度
    void SetPBRParameters(float metallic, float roughness, float occlusionStrength = 1.0f);

    /// @brief 法線マップを有効/無効にする
    /// @note Metallic/Roughness/AO はファクター×マップ乗算方式になったためフラグは法線のみ
    void SetNormalMapEnabled(bool enable);

    /// @brief マテリアルカラーを設定
    void SetMaterialColor(const CoreEngine::Vector4& color);

    /// @brief IBLを有効/無効にする
    void SetIBLEnabled(bool enable);

    /// @brief シェーディングモードを設定
    void SetShadingMode(CoreEngine::ShadingMode mode);

    /// @brief IBL強度を設定
    void SetIBLIntensity(float intensity);

private:
    std::string modelPath_;

protected:
    std::string GetModelPath() const override { return modelPath_; }
};

