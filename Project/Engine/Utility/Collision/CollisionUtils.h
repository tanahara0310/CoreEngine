#pragma once

#include "Engine/Math/MathCore.h"
#include "Engine/Math/BoundingBox.h"
#include <optional>


namespace CoreEngine
{
namespace CollisionUtils {

	//================================================
	// 基本的な形状構造体
	//================================================

	/// @brief 球体
	struct Sphere {
		Vector3 center;
		float radius;

		Sphere() : center({0.0f, 0.0f, 0.0f}), radius(1.0f) {}
		Sphere(const Vector3& center, float radius) : center(center), radius(radius) {}
	};

	/// @brief カプセル
	struct Capsule {
		Vector3 start;  // 開始点
		Vector3 end;    // 終了点
		float radius;   // 半径

		Capsule() : start({0.0f, 0.0f, 0.0f}), end({0.0f, 1.0f, 0.0f}), radius(0.5f) {}
		Capsule(const Vector3& start, const Vector3& end, float radius) 
			: start(start), end(end), radius(radius) {}
	};

	/// @brief レイ
	struct Ray {
		Vector3 origin;     // 原点
		Vector3 direction;  // 方向（正規化済み）

		Ray() : origin({0.0f, 0.0f, 0.0f}), direction({0.0f, 0.0f, 1.0f}) {}
		Ray(const Vector3& origin, const Vector3& direction) 
			: origin(origin), direction(direction) {}
	};

	/// @brief 平面
	struct Plane {
		Vector3 normal;     // 法線ベクトル（正規化済み）
		float distance;     // 原点からの距離

		Plane() : normal({0.0f, 1.0f, 0.0f}), distance(0.0f) {}
		Plane(const Vector3& normal, float distance) : normal(normal), distance(distance) {}
		Plane(const Vector3& normal, const Vector3& point);
	};

	/// @brief 線分
	struct LineSegment {
		Vector3 start;
		Vector3 end;

		LineSegment() : start({0.0f, 0.0f, 0.0f}), end({1.0f, 0.0f, 0.0f}) {}
		LineSegment(const Vector3& start, const Vector3& end) : start(start), end(end) {}
	};

	//================================================
	// 距離計算
	//================================================

	/// @brief 点と点の距離
	float DistancePointToPoint(const Vector3& p1, const Vector3& p2);

	/// @brief 点と平面の距離（符号付き）
	float DistancePointToPlane(const Vector3& point, const Plane& plane);

	/// @brief 点と線分の最短距離
	float DistancePointToLineSegment(const Vector3& point, const LineSegment& segment);

	/// @brief 点と球体の距離（球の表面までの距離、内部にある場合は負の値）
	float DistancePointToSphere(const Vector3& point, const Sphere& sphere);

	/// @brief 点とAABBの距離（AABB内部にある場合は0）
	float DistancePointToAABB(const Vector3& point, const BoundingBox& aabb);

	//================================================
	// 最近接点計算
	//================================================

	/// @brief 点から線分への最近接点
	Vector3 ClosestPointOnLineSegment(const Vector3& point, const LineSegment& segment);

	/// @brief 点から平面への最近接点
	Vector3 ClosestPointOnPlane(const Vector3& point, const Plane& plane);

	/// @brief 点からAABBへの最近接点
	Vector3 ClosestPointOnAABB(const Vector3& point, const BoundingBox& aabb);

	/// @brief 点から球体への最近接点（表面上の点）
	Vector3 ClosestPointOnSphere(const Vector3& point, const Sphere& sphere);

	//================================================
	// 当たり判定（基本形状）
	//================================================

	/// @brief 点とAABBの当たり判定
	bool IsColliding(const Vector3& point, const BoundingBox& aabb);

	/// @brief 点と球体の当たり判定
	bool IsColliding(const Vector3& point, const Sphere& sphere);

	/// @brief 球体と球体の当たり判定
	bool IsColliding(const Sphere& sphere1, const Sphere& sphere2);

	/// @brief AABBとAABBの当たり判定
	bool IsColliding(const BoundingBox& aabb1, const BoundingBox& aabb2);

	/// @brief 球体とAABBの当たり判定
	bool IsColliding(const Sphere& sphere, const BoundingBox& aabb);

	/// @brief カプセルと点の当たり判定
	bool IsColliding(const Vector3& point, const Capsule& capsule);

	/// @brief カプセルと球体の当たり判定
	bool IsColliding(const Capsule& capsule, const Sphere& sphere);

	/// @brief カプセルとカプセルの当たり判定
	bool IsColliding(const Capsule& capsule1, const Capsule& capsule2);

	//================================================
	// レイ当たり判定
	//================================================

	/// @brief レイと平面の交点計算
	/// @param ray レイ
	/// @param plane 平面
	/// @return 交点があれば交点座標、なければnullopt
	std::optional<Vector3> RayIntersectPlane(const Ray& ray, const Plane& plane);

	/// @brief レイと球体の交点計算
	/// @param ray レイ
	/// @param sphere 球体
	/// @return 最初の交点があれば交点座標、なければnullopt
	std::optional<Vector3> RayIntersectSphere(const Ray& ray, const Sphere& sphere);

	/// @brief レイとAABBの交点計算
	/// @param ray レイ
	/// @param aabb AABB
	/// @return 最初の交点があれば交点座標、なければnullopt
	std::optional<Vector3> RayIntersectAABB(const Ray& ray, const BoundingBox& aabb);

	/// @brief レイと球体の交点計算（距離も取得）
	/// @param ray レイ
	/// @param sphere 球体
	/// @param outDistance 交点までの距離（出力）
	/// @return 交点があれば交点座標、なければnullopt
	std::optional<Vector3> RayIntersectSphere(const Ray& ray, const Sphere& sphere, float& outDistance);

	//================================================
	// 便利関数
	//================================================

	/// @brief 点が平面のどちら側にあるかを判定
	/// @param point 点
	/// @param plane 平面
	/// @return 正の値：表側、負の値：裏側、0：平面上
	float PointPlaneDistance(const Vector3& point, const Plane& plane);

	/// @brief 値をクランプ
	/// @param value 値
	/// @param min 最小値
	/// @param max 最大値
	/// @return クランプされた値
	float Clamp(float value, float min, float max);

	/// @brief ベクトルをクランプ
	/// @param value ベクトル
	/// @param min 最小値
	/// @param max 最大値
	/// @return クランプされたベクトル
	Vector3 Clamp(const Vector3& value, const Vector3& min, const Vector3& max);

	/// @brief 線形補間
	/// @param start 開始値
	/// @param end 終了値
	/// @param t 補間係数（0.0f～1.0f）
	/// @return 補間された値
	float Lerp(float start, float end, float t);

	/// @brief ベクトルの線形補間
	/// @param start 開始ベクトル
	/// @param end 終了ベクトル
	/// @param t 補間係数（0.0f～1.0f）
	/// @return 補間されたベクトル
	Vector3 Lerp(const Vector3& start, const Vector3& end, float t);

	/// @brief 球面線形補間（SLERP）
	/// @param start 開始ベクトル（正規化済み）
	/// @param end 終了ベクトル（正規化済み）
	/// @param t 補間係数（0.0f～1.0f）
	/// @return 補間されたベクトル
	Vector3 Slerp(const Vector3& start, const Vector3& end, float t);

	/// @brief ベクトルをAABB内にクランプ
	/// @param vector ベクトル
	/// @param aabb AABB
	/// @return クランプされたベクトル
	Vector3 ClampToAABB(const Vector3& vector, const BoundingBox& aabb);

	/// @brief AABBの拡張
	/// @param aabb 元のAABB
	/// @param expansion 拡張量
	/// @return 拡張されたAABB
	BoundingBox ExpandAABB(const BoundingBox& aabb, float expansion);
	BoundingBox ExpandAABB(const BoundingBox& aabb, const Vector3& expansion);

	/// @brief AABBを点で拡張
	/// @param aabb 元のAABB
	/// @param point 追加する点
	/// @return 拡張されたAABB
	BoundingBox ExpandAABBWithPoint(const BoundingBox& aabb, const Vector3& point);

	/// @brief 複数の点からAABBを作成
	/// @param points 点の配列
	/// @param count 点の数
	/// @return 作成されたAABB
	BoundingBox CreateAABBFromPoints(const Vector3* points, size_t count);

}
}
