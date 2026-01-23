#pragma once

#include <vector>
#include "Math/Vector/Vector3.h"
#include "Engine/Graphics/Line/Line.h"

/// @brief 3Dスプライン曲線クラス
/// 3Dレールシューティングゲーム用のスプライン曲線を扱う汎用クラス

namespace CoreEngine
{
class Spline {
public:
	/// @brief コントロールポイント構造体
	struct ControlPoint {
		Vector3 position;   // 位置
		Vector3 tangent;    // 接線ベクトル（方向）
		float tension = 0.0f; // 張力（カーブの強さ）
	};

	/// @brief スプライン曲線の種類
	enum class SplineType {
		Linear,         // 線形補間
		CatmullRom,     // Catmull-Romスプライン
		Bezier,         // ベジエ曲線
		Hermite         // エルミート曲線
	};

public:
	/// @brief コンストラクタ
	/// @param type スプライン曲線の種類
	Spline(SplineType type = SplineType::CatmullRom);

	/// @brief デストラクタ
	~Spline() = default;

	/// @brief コントロールポイントを追加
	/// @param point 追加するコントロールポイント
	void AddControlPoint(const ControlPoint& point);

	/// @brief コントロールポイントを追加（位置のみ）
	/// @param position 追加する位置
	void AddControlPoint(const Vector3& position);

	/// @brief コントロールポイントをクリア
	void ClearControlPoints();

	/// @brief コントロールポイントを取得
	/// @param index インデックス
	/// @return コントロールポイント
	const ControlPoint& GetControlPoint(size_t index) const;

	/// @brief コントロールポイント数を取得
	/// @return コントロールポイント数
	size_t GetControlPointCount() const { return controlPoints_.size(); }

	/// @brief スプライン曲線上の位置を取得
	/// @param t パラメータ（0.0f〜1.0f）
	/// @return 位置
	Vector3 GetPosition(float t) const;

	/// @brief スプライン曲線上の接線ベクトルを取得
	/// @param t パラメータ（0.0f〜1.0f）
	/// @return 正規化された接線ベクトル
	Vector3 GetTangent(float t) const;

	/// @brief スプライン曲線上の法線ベクトルを取得
	/// @param t パラメータ（0.0f〜1.0f）
	/// @param upVector 上方向ベクトル（デフォルト：Y軸正方向）
	/// @return 正規化された法線ベクトル
	Vector3 GetNormal(float t, const Vector3& upVector = {0.0f, 1.0f, 0.0f}) const;

	/// @brief スプライン曲線の長さを取得（近似計算）
	/// @param segments 分割数（精度）
	/// @return 曲線の長さ
	float GetLength(int segments = 100) const;

	/// @brief 指定した距離での曲線上のパラメータを取得
	/// @param distance 距離
	/// @param segments 分割数（精度）
	/// @return パラメータt
	float GetParameterByDistance(float distance, int segments = 100) const;

	/// @brief 描画用の線分を生成
	/// @param segments 分割数
	/// @param color 線の色
	/// @param alpha 線のアルファ値
	/// @return Line配列
	std::vector<Line> GenerateLines(int segments = 50, const Vector3& color = {1.0f, 1.0f, 1.0f}, float alpha = 1.0f) const;

	/// @brief コントロールポイント表示用の線分を生成
	/// @param color 線の色
	/// @param alpha 線のアルファ値
	/// @return Line配列
	std::vector<Line> GenerateControlPointLines(const Vector3& color = {1.0f, 0.0f, 0.0f}, float alpha = 1.0f) const;

	/// @brief スプライン曲線の種類を設定
	/// @param type スプライン曲線の種類
	void SetSplineType(SplineType type) { splineType_ = type; }

	/// @brief スプライン曲線の種類を取得
	/// @return スプライン曲線の種類
	SplineType GetSplineType() const { return splineType_; }

	/// @brief ループするかどうかを設定
	/// @param loop ループフラグ
	void SetLoop(bool loop) { isLoop_ = loop; }

	/// @brief ループするかどうかを取得
	/// @return ループフラグ
	bool IsLoop() const { return isLoop_; }

private:
	/// @brief 線形補間での位置計算
	/// @param t パラメータ（0.0f〜1.0f）
	/// @return 位置
	Vector3 CalculateLinearPosition(float t) const;

	/// @brief Catmull-Romスプライン補間での位置計算
	/// @param t パラメータ（0.0f〜1.0f）
	/// @return 位置
	Vector3 CalculateCatmullRomPosition(float t) const;

	/// @brief ベジエ曲線補間での位置計算
	/// @param t パラメータ（0.0f〜1.0f）
	/// @return 位置
	Vector3 CalculateBezierPosition(float t) const;

	/// @brief エルミート曲線補間での位置計算
	/// @param t パラメータ（0.0f〜1.0f）
	/// @return 位置
	Vector3 CalculateHermitePosition(float t) const;

	/// @brief セグメント内での補間計算
	/// @param p0 点0
	/// @param p1 点1
	/// @param p2 点2
	/// @param p3 点3
	/// @param t パラメータ（0.0f〜1.0f）
	/// @return 補間された位置
	Vector3 CatmullRomInterpolation(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) const;

	/// @brief パラメータの正規化（ループ対応）
	/// @param t パラメータ
	/// @return 正規化されたパラメータ
	float NormalizeParameter(float t) const;

	/// @brief セグメントインデックスとローカルパラメータを取得
	/// @param t グローバルパラメータ
	/// @param segmentIndex 出力：セグメントインデックス
	/// @param localT 出力：ローカルパラメータ
	void GetSegmentInfo(float t, int& segmentIndex, float& localT) const;

private:
	std::vector<ControlPoint> controlPoints_;   // コントロールポイント配列
	SplineType splineType_;                     // スプライン曲線の種類
	bool isLoop_;                               // ループフラグ
};
}
