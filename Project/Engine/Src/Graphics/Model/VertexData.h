#pragma once

#include "Graphics/Shader/CBufferLayout.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine
{
/// @brief 頂点バッファ 1 要素分のレイアウト（シェーダー側の入力と一致させること）
struct VertexData {
    Vector4 position; // 頂点の位置
    Vector2 texcoord; // UV座標
    Vector3 normal;   // 法線ベクトル
    Vector3 tangent;  // タンジェント（接線）- ノーマルマップ用
    // 頂点アニメーション用の値（glTF の TEXCOORD_1.xy, TEXCOORD_2.xy。持たないモデルは 0）
    // 植物: (震えの振幅 m, 位相 0..1, しなりの振幅 m, 株全体の曲げの振幅 m)
    // 魚:   (吻端 0 → 尾 1, 体の横揺れの振幅 m, 胸びれの振幅 m, 胸びれの左右 ±1)
    // 使い方は Shaders/Include/Object/VertexAnimation.hlsli。末尾に置くのは、読まないシェーダーの
    // 入力レイアウト（リフレクションから APPEND_ALIGNED で組む）のオフセットを変えないため
    Vector4 animData;
};

// 頂点バッファ要素なので cbuffer の 16B 規則ではなく詰め込み規則で検証する
static constexpr Cb::Field kVertexDataFields[] = {
    CB_FIELD(VertexData, position), CB_FIELD(VertexData, texcoord), CB_FIELD(VertexData, normal),
    CB_FIELD(VertexData, tangent), CB_FIELD(VertexData, animData),
};
CB_VERIFY_STRIDE(VertexData, kVertexDataFields);
}
