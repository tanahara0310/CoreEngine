#pragma once

#include "Engine/ObjectCommon/GameObject.h"

/// @brief Sphereモデルオブジェクト

namespace CoreEngine
{
class SphereObject : public GameObject {
public:
	/// @brief 初期化処理
	/// @param engine エンジンシステムへのポインタ
	void Initialize();

	/// @brief 更新処理
	void Update() override;

	/// @brief 描画処理
	/// @param camera カメラ
	void Draw(const ICamera* camera) override;

#ifdef _DEBUG
	/// @brief オブジェクト名を取得
	/// @return オブジェクト名
	const char* GetObjectName() const override { return "Sphere"; }
#endif

	/// @brief トランスフォームを取得
	WorldTransform& GetTransform() { return transform_; }

	/// @brief モデルを取得
	Model* GetModel() { return model_.get(); }

	/// @brief PBRパラメータを設定
	/// @param metallic 金属性 (0.0-1.0)
	/// @param roughness 粗さ (0.0-1.0)
	/// @param ao 環境遮蔽 (0.0-1.0)
	void SetPBRParameters(float metallic, float roughness, float ao);

	/// @brief PBRを有効/無効にする
	/// @param enable true: PBR有効, false: 従来のライティング
	void SetPBREnabled(bool enable);

	/// @brief 環境マップを有効/無効にする
	/// @param enable true: 有効, false: 無効
	void SetEnvironmentMapEnabled(bool enable);

	/// @brief 環境マップの反射強度を設定
	/// @param intensity 反射強度 (0.0-1.0)
	void SetEnvironmentMapIntensity(float intensity);

	/// @brief マテリアルカラーを設定
	/// @param color カラー（RGBA）
	void SetMaterialColor(const Vector4& color);

	/// @brief IBLを有効/無効にする
	/// @param enable true: IBL有効, false: IBL無効
	void SetIBLEnabled(bool enable);

	/// @brief IBL強度を設定
	/// @param intensity IBL強度 (0.0-1.0)
	void SetIBLIntensity(float intensity);

#ifdef _DEBUG
	/// @brief ImGui拡張UI描画（PBR/IBLパラメータを表示）
	/// @return ImGuiで変更があった場合 true
	bool DrawImGuiExtended() override;
#endif
};
}
