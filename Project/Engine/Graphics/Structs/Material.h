#pragma once
#include "Matrix/Matrix4x4.h"
#include "Vector/Vector4.h"
#include "Vector/Vector3.h"
#include <cstdint>


namespace CoreEngine
{
struct Material {
    Vector4 color;
    int32_t enableLighting;
    float padding[3];
    Matrix4x4 uvTransform;
    float shininess; // 光沢度
    int shadingMode; // シェーディングモード(0: None, 1: Lambert, 2: Half-Lambert, 3: Toon)
    float toonThreshold; // トゥーンシェーディングの閾値 (0.0-1.0)
    float toonSmoothness; // トゥーンシェーディングの滑らかさ (0.0-0.5)
    int32_t enableDithering; // ディザリング有効化フラグ (0: 無効, 1: 有効)
    float ditheringScale; // ディザリングのスケール（デフォルト: 1.0f）
    
    float padding2[4]; // パディング調整
};
}
