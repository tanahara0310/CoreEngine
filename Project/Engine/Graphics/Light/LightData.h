#pragma once

#include "MathCore.h"
#include <cstdint>

/// @brief StructuredBuffer用のライトデータ構造体群
/// 新しいライトシステムで使用する統一された構造体

/// @brief ディレクショナルライトのデータ構造体（StructuredBuffer用）

namespace CoreEngine
{
    struct DirectionalLightData {
        Vector4 color;        // ライトの色
        Vector3 direction;    // ライトの方向
        float intensity;      // 輝度
        bool enabled;         // 有効フラグ
        Vector3 padding;      // パディング（16バイトアライメント）
    };

    /// @brief ポイントライトのデータ構造体（StructuredBuffer用）
    struct PointLightData {
        Vector4 color;        // 光源の色
        Vector3 position;     // 光源の位置
        float intensity;      // 光源の強度
        float radius;         // ライトの届く最大距離
        float decay;          // 光の減衰率
        bool enabled;         // 有効フラグ
        float padding;        // パディング（16バイトアライメント）
    };

    /// @brief スポットライトのデータ構造体（StructuredBuffer用）
    struct SpotLightData {
        Vector4 color;            // 光源の色
        Vector3 position;         // 光源の位置
        float intensity;          // 光源の強度
        Vector3 direction;        // 光源の向き
        float distance;           // スポットライトの届く距離
        float decay;              // 光の減衰率
        float cosAngle;           // スポットライトの角度
        float cosFalloffStart;    // スポットライトの角度の開始位置
        bool enabled;             // 有効フラグ
    };

    /// @brief エリアライトのデータ構造体（StructuredBuffer用）
    struct AreaLightData {
        Vector4 color;        // 光源の色
        Vector3 position;     // 光源の中心位置
        float intensity;      // 光源の強度
        Vector3 normal;       // 光源の向き（法線）
        float width;          // 矩形の幅
        Vector3 right;        // 矩形の右方向ベクトル
        float height;         // 矩形の高さ
        Vector3 up;           // 矩形の上方向ベクトル
        float range;          // ライトの届く最大距離
        bool enabled;         // 有効フラグ
        Vector3 padding;      // パディング（16バイトアライメント）
    };

    /// @brief ライトカウント用の定数バッファ構造体
    struct LightCounts {
        uint32_t directionalLightCount;
        uint32_t pointLightCount;
        uint32_t spotLightCount;
        uint32_t areaLightCount;
    };
}
