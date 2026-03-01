#pragma once

#include "Engine/ObjectCommon/GameObject.h"

/// @brief 汎用モデルオブジェクト（glTFなどのモデル読み込み用）
class ModelObject : public CoreEngine::GameObject {
public:
    /// @brief 初期化処理
    /// @param modelPath モデルファイルのパス（例: "SampleAssets/PblTestModel/ABeautifulGame.gltf"）
    void Initialize(const std::string& modelPath);

    /// @brief 更新処理
    void Update() override;

    /// @brief 描画処理
    /// @param camera カメラ
    void Draw(const CoreEngine::ICamera* camera) override;

    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "Model"; }

#ifdef _DEBUG
    /// @brief デバッグUI描画
    bool DrawImGuiExtended() override;
#endif

    /// @brief トランスフォームを取得
    CoreEngine::WorldTransform& GetTransform() { return transform_; }

    /// @brief モデルを取得
    CoreEngine::Model* GetModel() { return model_.get(); }

    /// @brief PBRパラメータを設定
    void SetPBRParameters(float metallic, float roughness, float ao);

    /// @brief PBRを有効/無効にする
    void SetPBREnabled(bool enable);

    /// @brief PBRテクスチャマップを有効/無効にする
    /// @param useNormal ノーマルマップを使用するか
    /// @param useMetallic メタリックマップを使用するか
    /// @param useRoughness ラフネスマップを使用するか
    /// @param useAO AOマップを使用するか
    void SetPBRTextureMapsEnabled(bool useNormal = true, bool useMetallic = true,
        bool useRoughness = true, bool useAO = true);

    /// @brief 環境マップを有効/無効にする
    void SetEnvironmentMapEnabled(bool enable);

    /// @brief 環境マップの反射強度を設定
    void SetEnvironmentMapIntensity(float intensity);

    /// @brief マテリアルカラーを設定
    void SetMaterialColor(const CoreEngine::Vector4& color);

    /// @brief IBLを有効/無効にする
    void SetIBLEnabled(bool enable);

    /// @brief IBL強度を設定
    void SetIBLIntensity(float intensity);

    /// @brief 環境マップY軸回転を設定
    void SetEnvironmentRotationY(float rotationY);
};

