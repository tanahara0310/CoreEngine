#pragma once

#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
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

    static constexpr Cb::Field kTransformationMatrixFields[] = {
        CB_FIELD(TransformationMatrix, WVP), CB_FIELD(TransformationMatrix, world),
        CB_FIELD(TransformationMatrix, worldInverseTranspose), CB_FIELD(TransformationMatrix, lightViewProjection),
        CB_FIELD(TransformationMatrix, prevWVP),
    };
    CB_VERIFY_LAYOUT(TransformationMatrix, kTransformationMatrixFields);
    // HLSL 側の照合（CB_BIND_HLSL）は入れていない。"gTransformationMatrix" という変数名を
    // 通常の 3D 描画（本構造体・320B）と SkyBox（WVP だけの 64B）が別レイアウトで共用しており、
    // 名前だけではどちらを指すか決められないため。
    // 照合を有効にしたい場合は HLSL 側の変数名を分けること。
}
