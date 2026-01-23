#pragma once

#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/Graphics/Primitive/PrimitivePlane.h"
#include "Engine/Graphics/Material/MaterialManager.h"
#include <memory>

// 前方宣言
namespace CoreEngine {
	class DirectXCommon;
	class ResourceFactory;
	struct TransformationMatrix;
}

/// @brief 丸影オブジェクト
/// @details キャラクターの下に表示する疑似的な丸い影
class CircleShadowObject : public CoreEngine::GameObject {
public:
	CircleShadowObject() = default;
	~CircleShadowObject() override = default;

	/// @brief 初期化
	/// @param shadowSize 影のサイズ（直径）
	void Initialize(float shadowSize = 2.0f);

	/// @brief 更新処理
	void Update() override;

	/// @brief 描画処理
	/// @param camera カメラ
	void Draw(const CoreEngine::ICamera* camera) override;

	/// @brief 描画パスタイプを取得
	CoreEngine::RenderPassType GetRenderPassType() const override { return CoreEngine::RenderPassType::Model; }

	/// @brief ブレンドモードを取得（加算ブレンドで影を表現）
	CoreEngine::BlendMode GetBlendMode() const override { return CoreEngine::BlendMode::kBlendModeNormal; }

	/// @brief 影の追従対象位置を設定
	/// @param targetPosition 追従対象のワールド位置
	void SetTargetPosition(const CoreEngine::Vector3& targetPosition);



	/// @brief 影のY座標を設定
	/// @param y Y座標
	void SetShadowY(float y) { shadowY_ = y; }

	/// @brief 影のサイズを設定
	/// @param size 影のサイズ（直径）
	void SetShadowSize(float size);

	/// @brief 影の濃さを設定
	/// @param alpha 影の濃さ（0.0〜1.0）
	void SetShadowAlpha(float alpha);

#ifdef _DEBUG
	/// @brief オブジェクト名を取得
	const char* GetObjectName() const override { return "CircleShadow"; }


	/// @brief ImGui拡張UI描画
	bool DrawImGuiExtended() override;
#endif

private:
	/// @brief WVP行列を更新
	void UpdateTransformationMatrix(const CoreEngine::ICamera* camera);

	// プリミティブPlane
	std::unique_ptr<CoreEngine::PrimitivePlane> plane_;

	// マテリアル
	std::unique_ptr<CoreEngine::MaterialManager> materialManager_;

	// WVP行列用リソース
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
	CoreEngine::TransformationMatrix* wvpData_ = nullptr;

	// 影のパラメータ
	float shadowY_ = 0.0f;      // 影のY座標
	float shadowAlpha_ = 0.5f;  // 影の濃さ
	float shadowSize_ = 2.0f;   // 影のサイズ（直径）
};
