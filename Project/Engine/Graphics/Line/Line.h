#pragma once

#include "Math/Vector/Vector3.h"

/// @brief ライン構造体（汎用的なライン表現）

namespace CoreEngine
{
struct Line {
	Vector3 start;   // 開始点
	Vector3 end;     // 終了点
	Vector3 color;   // 色 (RGB)
	float alpha;     // 透明度

	/// @brief デフォルトコンストラクタ
	Line() 
		: start(0.0f, 0.0f, 0.0f)
		, end(0.0f, 0.0f, 0.0f)
		, color(1.0f, 1.0f, 1.0f)
		, alpha(1.0f) 
	{}

	/// @brief パラメータ付きコンストラクタ
	Line(const Vector3& s, const Vector3& e, const Vector3& c = {1.0f, 1.0f, 1.0f}, float a = 1.0f)
		: start(s), end(e), color(c), alpha(a) 
	{}
};
}
