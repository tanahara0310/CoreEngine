#pragma once

#include "Graphics/Shader/CBufferLayout.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine
{
struct VertexData {
    Vector4 position; // 頂点の位置
    Vector2 texcoord; // UV座標
    Vector3 normal;   // 法線ベクトル
    Vector3 tangent;  // タンジェント（接線）- ノーマルマップ用
};

// 頂点バッファ要素なので cbuffer の 16B 規則ではなく詰め込み規則で検証する
static constexpr Cb::Field kVertexDataFields[] = {
    CB_FIELD(VertexData, position), CB_FIELD(VertexData, texcoord), CB_FIELD(VertexData, normal),
    CB_FIELD(VertexData, tangent),
};
CB_VERIFY_STRIDE(VertexData, kVertexDataFields);
}
