#pragma once

#include "Vector/Vector2.h"
#include "Vector/Vector3.h"
#include "Vector/Vector4.h"

namespace CoreEngine
{
struct VertexData {
	Vector4 position; // 頂点の位置
	Vector2 texcoord; // UV座標
	Vector3 normal;   // 法線ベクトル
	Vector3 tangent;  // タンジェント（接線）- ノーマルマップ用
};
}
