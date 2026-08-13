#pragma once
#include "Graphics/Shader/CBufferLayout.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector4.h"

// UI 専用マテリアル構造体（HLSL 側 cbuffer Material と完全一致）
//   現状はスプライトと同レイアウトだが、将来 UI 固有フィールド（cornerRadius 等）を
//   追加した際に Sprite 側に影響を与えないために独立した構造体として定義する

namespace CoreEngine
{
    struct UIMaterial {
        Vector4   color;
        Matrix4x4 uvTransform;
    };

    static constexpr Cb::Field kUIMaterialFields[] = {
        CB_FIELD(UIMaterial, color), CB_FIELD(UIMaterial, uvTransform),
    };
    CB_VERIFY_LAYOUT(UIMaterial, kUIMaterialFields);
}
