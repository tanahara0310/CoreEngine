#pragma once

#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Math/MathCore.h"
#include <cstdint>

/// @brief StructuredBuffer 用のライトデータ構造体群（純粋な GPU 転送フォーマット）
/// オーサリング表現は Light.h の Light 構造体が担い、LightManager::UpdateAll() が
/// 物理単位（lx / cd / nt）→ シェーダー単位への変換・cos 変換・基底導出を行って
/// 本構造体へ詰め替える。CPU 専用の状態はここに置かないこと。
/// レイアウトは HLSL 側 LightStructures.hlsli と 1:1 で一致させる（bool は uint32_t）。

namespace CoreEngine
{
    /// @brief ディレクショナルライトのデータ構造体（StructuredBuffer用）
    struct DirectionalLightData {
        Vector4 color;        // ライトの色（大気透過率適用済みの実効色）
        Vector3 direction;    // ライトの方向（正規化済み）
        float intensity;      // 輝度（シェーダー単位。lx から変換済み）
        uint32_t enabled;     // 有効フラグ（HLSL の bool = 4 バイト）
        float padding[3];     // パディング（HLSL 側 float3 padding）
    };
    static_assert(sizeof(DirectionalLightData) == 48,
        "DirectionalLightData は HLSL 側 LightStructures.hlsli の 48 バイトストライドと一致させること");

    static constexpr Cb::Field kDirectionalLightDataFields[] = {
        CB_FIELD(DirectionalLightData, color), CB_FIELD(DirectionalLightData, direction),
        CB_FIELD(DirectionalLightData, intensity), CB_FIELD(DirectionalLightData, enabled),
        CB_FIELD(DirectionalLightData, padding),
    };
    CB_VERIFY_STRIDE(DirectionalLightData, kDirectionalLightDataFields);

    /// @brief ポイントライトのデータ構造体（StructuredBuffer用）
    struct PointLightData {
        Vector4 color;        // 光源の色
        Vector3 position;     // 光源の位置
        float intensity;      // 光源の強度（シェーダー単位。cd から変換済み。シェーダーで 1/d² を乗算）
        float radius;         // ライトの届く最大距離 [m]
        float decay;          // 旧減衰率。物理逆二乗化に伴い未使用（レイアウト互換のため残置。常に 1）
        uint32_t enabled;     // 有効フラグ
        float padding;        // パディング（16バイトアライメント）
    };
    static_assert(sizeof(PointLightData) == 48,
        "PointLightData は HLSL 側 LightStructures.hlsli の 48 バイトストライドと一致させること");

    static constexpr Cb::Field kPointLightDataFields[] = {
        CB_FIELD(PointLightData, color), CB_FIELD(PointLightData, position), CB_FIELD(PointLightData, intensity),
        CB_FIELD(PointLightData, radius), CB_FIELD(PointLightData, decay), CB_FIELD(PointLightData, enabled),
        CB_FIELD(PointLightData, padding),
    };
    CB_VERIFY_STRIDE(PointLightData, kPointLightDataFields);

    /// @brief スポットライトのデータ構造体（StructuredBuffer用）
    struct SpotLightData {
        Vector4 color;            // 光源の色
        Vector3 position;         // 光源の位置
        float intensity;          // 光源の強度（シェーダー単位。cd から変換済み）
        Vector3 direction;        // 光源の向き（正規化済み）
        float distance;           // スポットライトの届く距離 [m]
        float decay;              // 旧減衰率。物理逆二乗化に伴い未使用（レイアウト互換のため残置。常に 1）
        float cosAngle;           // 外角の余弦（cos(outerConeAngle)）
        float cosFalloffStart;    // 減衰開始角の余弦（cos(innerConeAngle)）
        uint32_t enabled;         // 有効フラグ
    };
    static_assert(sizeof(SpotLightData) == 64,
        "SpotLightData は HLSL 側 LightStructures.hlsli の 64 バイトストライドと一致させること");

    static constexpr Cb::Field kSpotLightDataFields[] = {
        CB_FIELD(SpotLightData, color), CB_FIELD(SpotLightData, position), CB_FIELD(SpotLightData, intensity),
        CB_FIELD(SpotLightData, direction), CB_FIELD(SpotLightData, distance), CB_FIELD(SpotLightData, decay),
        CB_FIELD(SpotLightData, cosAngle), CB_FIELD(SpotLightData, cosFalloffStart),
        CB_FIELD(SpotLightData, enabled),
    };
    CB_VERIFY_STRIDE(SpotLightData, kSpotLightDataFields);

    /// @brief エリアライトのデータ構造体（StructuredBuffer用）
    struct AreaLightData {
        Vector4 color;        // 光源の色
        Vector3 position;     // 光源の中心位置
        float intensity;      // 光源の強度（シェーダー単位。nt × 面積 から変換済み）
        Vector3 normal;       // 光源の向き（法線。正規化済み）
        float width;          // 矩形の幅 [m]
        Vector3 right;        // 矩形の右方向ベクトル（法線から導出した正規直交基底）
        float height;         // 矩形の高さ [m]
        Vector3 up;           // 矩形の上方向ベクトル（法線から導出した正規直交基底）
        float range;          // ライトの届く最大距離 [m]
        uint32_t enabled;     // 有効フラグ
        Vector3 padding;      // パディング（16バイトアライメント）
    };
    static_assert(sizeof(AreaLightData) == 96,
        "AreaLightData は HLSL 側 LightStructures.hlsli の 96 バイトストライドと一致させること");

    static constexpr Cb::Field kAreaLightDataFields[] = {
        CB_FIELD(AreaLightData, color), CB_FIELD(AreaLightData, position), CB_FIELD(AreaLightData, intensity),
        CB_FIELD(AreaLightData, normal), CB_FIELD(AreaLightData, width), CB_FIELD(AreaLightData, right),
        CB_FIELD(AreaLightData, height), CB_FIELD(AreaLightData, up), CB_FIELD(AreaLightData, range),
        CB_FIELD(AreaLightData, enabled), CB_FIELD(AreaLightData, padding),
    };
    CB_VERIFY_STRIDE(AreaLightData, kAreaLightDataFields);

    /// @brief ライトカウント用の定数バッファ構造体
    struct LightCounts {
        uint32_t directionalLightCount;
        uint32_t pointLightCount;
        uint32_t spotLightCount;
        uint32_t areaLightCount;
    };

    static constexpr Cb::Field kLightCountsFields[] = {
        CB_FIELD(LightCounts, directionalLightCount), CB_FIELD(LightCounts, pointLightCount),
        CB_FIELD(LightCounts, spotLightCount), CB_FIELD(LightCounts, areaLightCount),
    };
    CB_VERIFY_LAYOUT(LightCounts, kLightCountsFields);
    CB_BIND_HLSL(LightCounts, kLightCountsFields, "gLightCounts");
}
