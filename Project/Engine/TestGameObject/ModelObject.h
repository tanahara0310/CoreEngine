#pragma once

#include "Engine/ObjectCommon/GameObject.h"

/// @brief 汎用モデルオブジェクト（glTFなどのモデル読み込み用）

namespace CoreEngine
{
class ModelObject : public GameObject {
public:
    /// @brief 初期化処理
    /// @param modelPath モデルファイルのパス（例: "SampleAssets/PblTestModel/ABeautifulGame.gltf"）
    void Initialize(const std::string& modelPath);

    /// @brief 更新処理
    void Update() override;

    /// @brief 描画処理
    /// @param camera カメラ
    void Draw(const ICamera* camera) override;

#ifdef _DEBUG
    /// @brief オブジェクト名を取得
    /// @return オブジェクト名
    const char* GetObjectName() const override { return "Model"; }
#endif

    /// @brief トランスフォームを取得
    WorldTransform& GetTransform() { return transform_; }

    /// @brief モデルを取得
    Model* GetModel() { return model_.get(); }

    /// @brief PBRパラメータを設定
    void SetPBRParameters(float metallic, float roughness, float ao);

    /// @brief PBRを有効/無効にする
    void SetPBREnabled(bool enable);

    /// @brief 環境マップを有効/無効にする
    void SetEnvironmentMapEnabled(bool enable);

    /// @brief 環境マップの反射強度を設定
    void SetEnvironmentMapIntensity(float intensity);

    /// @brief マテリアルカラーを設定
    void SetMaterialColor(const Vector4& color);

    /// @brief IBLを有効/無効にする
    void SetIBLEnabled(bool enable);

    /// @brief IBL強度を設定
    void SetIBLIntensity(float intensity);

    /// @brief 環境マップY軸回転を設定
    void SetEnvironmentRotationY(float rotationY);
};
}
