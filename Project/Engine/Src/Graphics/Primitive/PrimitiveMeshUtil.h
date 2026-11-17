#pragma once

#include "Graphics/Model/ModelData.h"

#include <cstdint>

namespace CoreEngine
{
    /// @brief プリミティブ形状の生成で共通に使う処理
    namespace PrimitiveMeshUtil
    {
        /// @brief 位置・UV・法線・接線を埋めた頂点を作る（頂点アニメーション用の値は 0）
        VertexData MakeVertex(const Vector4& position, const Vector2& texcoord,
            const Vector3& normal, const Vector3& tangent);

        /// @brief 三角形 1 枚分のインデックスを末尾へ足す
        void PushTriangle(ModelData& data, uint32_t i0, uint32_t i1, uint32_t i2);

        /// @brief 全インデックスを 1 つのサブメッシュにまとめ、既定のマテリアルと単位行列のルートノードを付ける
        /// @param subMeshName サブメッシュの名前
        void FinishSingleMesh(ModelData& data, const char* subMeshName);
    }
}
