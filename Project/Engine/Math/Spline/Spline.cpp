#include "Spline.h"
#include "Engine/Math/MathCore.h"
#include <algorithm>
#include <cmath>

namespace CoreEngine
{

// 新しい数学ライブラリを使用
using namespace CoreEngine::MathCore;

Spline::Spline(SplineType type)
	: splineType_(type)
	, isLoop_(false)
{
}

void Spline::AddControlPoint(const ControlPoint& point)
{
	controlPoints_.push_back(point);
}

void Spline::AddControlPoint(const Vector3& position)
{
	ControlPoint point;
	point.position = position;
	point.tangent = Vector3{ 0.0f, 0.0f, 1.0f }; // デフォルトの接線ベクトル
	point.tension = 0.0f;
	controlPoints_.push_back(point);
}

void Spline::ClearControlPoints()
{
	controlPoints_.clear();
}

const Spline::ControlPoint& Spline::GetControlPoint(size_t index) const
{
	return controlPoints_[index];
}

Vector3 Spline::GetPosition(float t) const
{
	if (controlPoints_.size() < 2) {
		return controlPoints_.empty() ? Vector3{0.0f, 0.0f, 0.0f} : controlPoints_[0].position;
	}

	t = NormalizeParameter(t);

	switch (splineType_) {
	case SplineType::Linear:
		return CalculateLinearPosition(t);
	case SplineType::CatmullRom:
		return CalculateCatmullRomPosition(t);
	case SplineType::Bezier:
		return CalculateBezierPosition(t);
	case SplineType::Hermite:
		return CalculateHermitePosition(t);
	default:
		return CalculateCatmullRomPosition(t);
	}
}

Vector3 Spline::GetTangent(float t) const
{
	if (controlPoints_.size() < 2) {
		return Vector3{ 0.0f, 0.0f, 1.0f };
	}

	// 微小な差分を使って接線ベクトルを計算
	const float epsilon = 0.001f;
	Vector3 p1 = GetPosition(t - epsilon);
	Vector3 p2 = GetPosition(t + epsilon);
	Vector3 tangent = Vector::Subtract(p2, p1);
	return Vector::Normalize(tangent);
}

Vector3 Spline::GetNormal(float t, const Vector3& upVector) const
{
	Vector3 tangent = GetTangent(t);
	Vector3 binormal = Vector::Normalize(Vector::Cross(tangent, upVector));
	return Vector::Normalize(Vector::Cross(binormal, tangent));
}

float Spline::GetLength(int segments) const
{
	if (controlPoints_.size() < 2) {
		return 0.0f;
	}

	float length = 0.0f;
	float step = 1.0f / static_cast<float>(segments);

	Vector3 prevPos = GetPosition(0.0f);
	for (int i = 1; i <= segments; ++i) {
		float t = static_cast<float>(i) * step;
		Vector3 currentPos = GetPosition(t);
		length += Vector::Length(Vector::Subtract(currentPos, prevPos));
		prevPos = currentPos;
	}

	return length;
}

float Spline::GetParameterByDistance(float distance, int segments) const
{
	if (controlPoints_.size() < 2 || distance <= 0.0f) {
		return 0.0f;
	}

	float totalDistance = 0.0f;
	float step = 1.0f / static_cast<float>(segments);

	Vector3 prevPos = GetPosition(0.0f);
	for (int i = 1; i <= segments; ++i) {
		float t = static_cast<float>(i) * step;
		Vector3 currentPos = GetPosition(t);
		float segmentDistance = Vector::Length(Vector::Subtract(currentPos, prevPos));
		
		if (totalDistance + segmentDistance >= distance) {
			// 線形補間で正確な位置を計算
			float ratio = (distance - totalDistance) / segmentDistance;
			return static_cast<float>(i - 1) * step + ratio * step;
		}
		
		totalDistance += segmentDistance;
		prevPos = currentPos;
	}

	return 1.0f; // 曲線の終端
}

std::vector<Line> Spline::GenerateLines(int segments, const Vector3& color, float alpha) const
{
	std::vector<Line> lines;
	
	if (controlPoints_.size() < 2) {
		return lines;
	}

	float step = 1.0f / static_cast<float>(segments);
	Vector3 prevPos = GetPosition(0.0f);

	for (int i = 1; i <= segments; ++i) {
		float t = static_cast<float>(i) * step;
		Vector3 currentPos = GetPosition(t);
		
		Line line;
		line.start = prevPos;
		line.end = currentPos;
		line.color = color;
		line.alpha = alpha;
		lines.push_back(line);
		
		prevPos = currentPos;
	}

	return lines;
}

std::vector<Line> Spline::GenerateControlPointLines(const Vector3& color, float alpha) const
{
	std::vector<Line> lines;
	
	if (controlPoints_.size() < 2) {
		return lines;
	}

	// コントロールポイント間を直線で結ぶ
	for (size_t i = 0; i < controlPoints_.size() - 1; ++i) {
		Line line;
		line.start = controlPoints_[i].position;
		line.end = controlPoints_[i + 1].position;
		line.color = color;
		line.alpha = alpha;
		lines.push_back(line);
	}

	// ループの場合は最後と最初を結ぶ
	if (isLoop_ && controlPoints_.size() > 2) {
		Line line;
		line.start = controlPoints_.back().position;
		line.end = controlPoints_.front().position;
		line.color = color;
		line.alpha = alpha;
		lines.push_back(line);
	}

	return lines;
}

Vector3 Spline::CalculateLinearPosition(float t) const
{
	int segmentIndex;
	float localT;
	GetSegmentInfo(t, segmentIndex, localT);

	size_t index0 = static_cast<size_t>(segmentIndex);
	size_t index1 = (index0 + 1) % controlPoints_.size();

	if (!isLoop_ && index1 >= controlPoints_.size()) {
		return controlPoints_.back().position;
	}

	// 線形補間
	Vector3 pos0 = controlPoints_[index0].position;
	Vector3 pos1 = controlPoints_[index1].position;
	Vector3 diff = Vector::Subtract(pos1, pos0);
	Vector3 scaled = { diff.x * localT, diff.y * localT, diff.z * localT };
	return Vector::Add(pos0, scaled);
}

Vector3 Spline::CalculateCatmullRomPosition(float t) const
{
	if (controlPoints_.size() < 4 && !isLoop_) {
		return CalculateLinearPosition(t);
	}

	int segmentIndex;
	float localT;
	GetSegmentInfo(t, segmentIndex, localT);

	size_t numPoints = controlPoints_.size();
	
	// インデックスの計算（ループ対応）
	size_t i0, i1, i2, i3;
	if (isLoop_) {
		i0 = (segmentIndex - 1 + numPoints) % numPoints;
		i1 = segmentIndex % numPoints;
		i2 = (segmentIndex + 1) % numPoints;
		i3 = (segmentIndex + 2) % numPoints;
	} else {
		int maxIndex = static_cast<int>(numPoints) - 1;
		i0 = (std::max)(0, segmentIndex - 1);
		i1 = segmentIndex;
		i2 = (std::min)(maxIndex, segmentIndex + 1);
		i3 = (std::min)(maxIndex, segmentIndex + 2);
	}

	return CatmullRomInterpolation(
		controlPoints_[i0].position,
		controlPoints_[i1].position,
		controlPoints_[i2].position,
		controlPoints_[i3].position,
		localT
	);
}

Vector3 Spline::CalculateBezierPosition(float t) const
{
	// 簡単なベジエ曲線実装（4点ベジエ）
	if (controlPoints_.size() < 4) {
		return CalculateLinearPosition(t);
	}

	// 最初の4点を使用した3次ベジエ曲線
	const Vector3& p0 = controlPoints_[0].position;
	const Vector3& p1 = controlPoints_[1].position;
	const Vector3& p2 = controlPoints_[2].position;
	const Vector3& p3 = controlPoints_[3].position;

	float t2 = t * t;
	float t3 = t2 * t;
	float mt = 1.0f - t;
	float mt2 = mt * mt;
	float mt3 = mt2 * mt;

	Vector3 result0 = { p0.x * mt3, p0.y * mt3, p0.z * mt3 };
	Vector3 result1 = { p1.x * (3.0f * mt2 * t), p1.y * (3.0f * mt2 * t), p1.z * (3.0f * mt2 * t) };
	Vector3 result2 = { p2.x * (3.0f * mt * t2), p2.y * (3.0f * mt * t2), p2.z * (3.0f * mt * t2) };
	Vector3 result3 = { p3.x * t3, p3.y * t3, p3.z * t3 };

	return Vector::Add(Vector::Add(Vector::Add(result0, result1), result2), result3);
}

Vector3 Spline::CalculateHermitePosition(float t) const
{
	int segmentIndex;
	float localT;
	GetSegmentInfo(t, segmentIndex, localT);

	size_t index0 = static_cast<size_t>(segmentIndex);
	size_t index1 = (index0 + 1) % controlPoints_.size();

	if (!isLoop_ && index1 >= controlPoints_.size()) {
		return controlPoints_.back().position;
	}

	const Vector3& p0 = controlPoints_[index0].position;
	const Vector3& p1 = controlPoints_[index1].position;
	const Vector3& t0 = controlPoints_[index0].tangent;
	const Vector3& t1 = controlPoints_[index1].tangent;

	float t2 = localT * localT;
	float t3 = t2 * localT;

	float h1 = 2.0f * t3 - 3.0f * t2 + 1.0f;
	float h2 = -2.0f * t3 + 3.0f * t2;
	float h3 = t3 - 2.0f * t2 + localT;
	float h4 = t3 - t2;

	Vector3 result0 = { p0.x * h1, p0.y * h1, p0.z * h1 };
	Vector3 result1 = { p1.x * h2, p1.y * h2, p1.z * h2 };
	Vector3 result2 = { t0.x * h3, t0.y * h3, t0.z * h3 };
	Vector3 result3 = { t1.x * h4, t1.y * h4, t1.z * h4 };

	return Vector::Add(Vector::Add(Vector::Add(result0, result1), result2), result3);
}

Vector3 Spline::CatmullRomInterpolation(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) const
{
	float t2 = t * t;
	float t3 = t2 * t;

	// Catmull-Rom基底関数
	float b0 = -0.5f * t3 + t2 - 0.5f * t;
	float b1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
	float b2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
	float b3 = 0.5f * t3 - 0.5f * t2;

	Vector3 result0 = { p0.x * b0, p0.y * b0, p0.z * b0 };
	Vector3 result1 = { p1.x * b1, p1.y * b1, p1.z * b1 };
	Vector3 result2 = { p2.x * b2, p2.y * b2, p2.z * b2 };
	Vector3 result3 = { p3.x * b3, p3.y * b3, p3.z * b3 };

	return Vector::Add(Vector::Add(Vector::Add(result0, result1), result2), result3);
}

float Spline::NormalizeParameter(float t) const
{
	if (isLoop_) {
		// ループの場合は0.0f〜1.0fの範囲で繰り返し
		while (t < 0.0f) t += 1.0f;
		while (t >= 1.0f) t -= 1.0f;
	} else {
		// 非ループの場合は0.0f〜1.0fでクランプ
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
	}
	return t;
}

void Spline::GetSegmentInfo(float t, int& segmentIndex, float& localT) const
{
	size_t numSegments = isLoop_ ? controlPoints_.size() : controlPoints_.size() - 1;
	
	if (numSegments == 0) {
		segmentIndex = 0;
		localT = 0.0f;
		return;
	}

	float segmentLength = 1.0f / static_cast<float>(numSegments);
	segmentIndex = static_cast<int>(t / segmentLength);
	localT = (t - static_cast<float>(segmentIndex) * segmentLength) / segmentLength;

	// 境界値の処理
	if (segmentIndex >= static_cast<int>(numSegments)) {
		segmentIndex = static_cast<int>(numSegments) - 1;
		localT = 1.0f;
	}
}
}
