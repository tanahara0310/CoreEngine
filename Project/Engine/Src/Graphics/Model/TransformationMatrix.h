#pragma once

#include "Math/Matrix/Matrix4x4.h"


namespace CoreEngine
{
    // ConstantBufferは256バイトアライメントが推奨（16バイトの倍数が必須）
    struct TransformationMatrix {
        Matrix4x4 WVP;                        // 64バイト
        Matrix4x4 world;                      // 64バイト
        Matrix4x4 worldInverseTranspose;      // 64バイト
        Matrix4x4 lightViewProjection;        // 64バイト（シャドウマップ用）
        Matrix4x4 prevWVP;                    // 64バイト（モーションベクター用: 前フレームWVP）
        // 合計320バイト（16バイトの倍数）
    };
}
