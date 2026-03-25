#pragma once

#include "ObjectCommon/Model/ModelGameObject.h"

/// @brief 汎用モデルオブジェクト（glTFなどのモデル読み込み用）
class ModelObject : public CoreEngine::ModelGameObject {
public:
    /// @brief 初期化処理
    /// @param modelPath モデルファイルのパス
    void Initialize(const std::string& modelPath);

    /// @brief ブレンドモードを取得
    /// @details α<1 かつディザリングOFF の場合は自動でアルファブレンドに切り替える
    CoreEngine::BlendMode GetBlendMode() const override;

    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "Model"; }

    /// @brief マテリアルインスタンスを直接取得（パラメータ直接操作用）
    /// @return MaterialInstance へのポインタ（未初期化の場合 nullptr）
    CoreEngine::MaterialInstance* GetMaterial();

    /// @brief PBRパラメータを設定
    void SetPBRParameters(float metallic, float roughness, float ao);

    /// @brief PBRテクスチャマップを有効/無効にする
    /// @param useNormal ノーマルマップを使用するか
    /// @param useMetallic メタリックマップを使用するか
    /// @param useRoughness ラフネスマップを使用するか
    /// @param useAO AOマップを使用するか
    void SetPBRTextureMapsEnabled(bool useNormal = true, bool useMetallic = true,
        bool useRoughness = true, bool useAO = true);

    /// @brief マテリアルカラーを設定
    void SetMaterialColor(const CoreEngine::Vector4& color);

    /// @brief IBLを有効/無効にする
    void SetIBLEnabled(bool enable);

    /// @brief IBL強度を設定
    void SetIBLIntensity(float intensity);

private:
    std::string modelPath_;

protected:
    std::string GetModelPath() const override { return modelPath_; }
};

