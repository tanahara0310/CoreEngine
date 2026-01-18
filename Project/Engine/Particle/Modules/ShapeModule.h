#pragma once

#include "ParticleModule.h"
#include "MathCore.h"

/// @brief パーティクルの形状モジュール（Unity Shape Module相当）
/// エミッターの形状からパーティクルの生成位置を決定します

namespace CoreEngine
{
class ShapeModule : public ParticleModule {
public:
	/// @brief 形状タイプ
	enum class ShapeType {
		Point,       // 点
		Box,            // ボックス
		Sphere,   // 球体
		Circle,         // 円
		Cone,      // コーン
		Hemisphere,     // 半球
		Ring,           // リング
		Line,    // 線
		Cylinder,       // 円柱
		Edge,  // エッジ（球体の表面）
		CircleHalf      // 半円
	};

	/// @brief 円の平面タイプ
	enum class CirclePlane {
		XZ,// XZ平面（地面に平行）
		XY,  // XY平面（垂直、Z固定）
		YZ // YZ平面（垂直、X固定）
	};

	/// @brief 形状データ
	struct ShapeData {
		ShapeType shapeType = ShapeType::Point;         // 形状タイプ
		Vector3 scale = { 1.0f, 1.0f, 1.0f };  // スケール（Box用）
		float radius = 1.0f;       // 半径（Sphere, Circle, Cone, Ring用）
		float innerRadius = 0.5f;   // 内径（Ring用）
		float height = 2.0f; // 高さ（Cone, Cylinder用）
		float angle = 25.0f;  // 角度（Cone用、度数）
		float randomPositionRange = 0.0f;      // ランダム位置範囲
		bool emitFromSurface = false;         // 表面からのみ放出するか
		Vector3 emissionDirection = { 0.0f, 1.0f, 0.0f }; // 放出方向（Line用）
		CirclePlane circlePlane = CirclePlane::XZ;      // 円の平面
	};

	ShapeModule();
	~ShapeModule() = default;

	/// @brief 形状データを設定
	/// @param data 形状データ
	void SetShapeData(const ShapeData& data) { shapeData_ = data; }

	/// @brief 形状データを取得
	/// @return 形状データの参照
	const ShapeData& GetShapeData() const { return shapeData_; }

	/// @brief 形状データを取得（書き換え可能）
	/// @return 形状データの参照
	ShapeData& GetShapeData() { return shapeData_; }

	/// @brief 形状に基づいてパーティクルの生成位置を生成
	/// @param emitterPosition エミッターの位置
	/// @return 生成位置
	Vector3 GeneratePosition(const Vector3& emitterPosition);

	/// @brief エミッター形状をデバッグ描画
	/// @param pipeline ラインレンダラーパイプライン
	/// @param camera カメラ
	/// @param emitterPosition エミッターの位置
	void DrawEmitterShape(class LineRendererPipeline* pipeline, const class ICamera* camera,
		const Vector3& emitterPosition);

	/// @brief デバッグ描画の有効/無効を設定
	/// @param enabled 有効にする場合true
	void SetDebugDraw(bool enabled) { debugDraw_ = enabled; }

	/// @brief デバッグ描画が有効か取得
	/// @return 有効な場合true
	bool IsDebugDrawEnabled() const { return debugDraw_; }

#ifdef _DEBUG
	/// @brief ImGuiデバッグ表示
	/// @return UIに変更があった場合true
	bool ShowImGui() override;
#endif

private:
	ShapeData shapeData_;
	bool debugDraw_ = false;  // デバッグ描画フラグ

	// 各形状の位置生成関数
	Vector3 GeneratePointPosition(const Vector3& emitterPosition);
	Vector3 GenerateBoxPosition(const Vector3& emitterPosition);
	Vector3 GenerateSpherePosition(const Vector3& emitterPosition);
	Vector3 GenerateCirclePosition(const Vector3& emitterPosition);
	Vector3 GenerateConePosition(const Vector3& emitterPosition);
	Vector3 GenerateHemispherePosition(const Vector3& emitterPosition);
	Vector3 GenerateRingPosition(const Vector3& emitterPosition);
	Vector3 GenerateLinePosition(const Vector3& emitterPosition);
	Vector3 GenerateCylinderPosition(const Vector3& emitterPosition);
	Vector3 GenerateEdgePosition(const Vector3& emitterPosition);
	Vector3 GenerateCircleHalfPosition(const Vector3& emitterPosition);

#ifdef _DEBUG

	/// @brief 半球をデバッグ描画
	void DrawHemisphere(class LineRenderer* lineRenderer, const class ICamera* camera,
		const Vector3& center, float radius, const Vector3& color, float alpha);

#endif // _DEBUG
};
}
