#pragma once
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector4.h"
#include "Math/Vector/Vector3.h"
#include <cstdint>


namespace CoreEngine
{
    /// @brief GPU定数バッファに送信するマテリアルパラメータ（PBR専用）
    /// @note glTF 準拠の「ファクター × テクスチャ」乗算方式。
    ///       テクスチャが無いマテリアルは白1x1フォールバックがバインドされるため、
    ///       ファクター値がそのまま最終値になる。
    /// @note IBL の有効/無効はシーン側（IBLマップの有無）で決まる。
    ///       マテリアルは iblIntensity のみ保持し、0 で個別オプトアウトできる。
    /// @note シェーダー側定義は Shaders/Include/Object/ObjectMaterial.hlsli と
    ///       メモリレイアウトを一致させること。
    struct MaterialConstants {
        Vector4 color;              ///< ベースカラーファクター (RGBA)。ベースカラーテクスチャと乗算
        Matrix4x4 uvTransform;      ///< UVトランスフォーム行列

        // ===== PBR Factors =====
        float metallic;             ///< 金属性ファクター。MRテクスチャの B チャネルと乗算
        float roughness;            ///< 粗さファクター。MRテクスチャの G チャネルと乗算
        float occlusionStrength;    ///< AOマップ適用強度 (0=無効, 1=フル適用)
        int32_t useNormalMap;       ///< 法線マップ使用フラグ（法線のみ乗算合成不可のためフラグ制御）

        Vector3 emissiveFactor;     ///< エミッシブファクター。エミッシブテクスチャと乗算
        int32_t enableLighting;     ///< 0=アンリット, 1=PBRライティング有効

        int32_t enableDithering;    ///< ディザリング有効フラグ (透明・葉など)
        float ditheringScale;       ///< ディザリングスケール
        float alphaCutoff;          ///< discard 判定に使用するアルファしきい値（デフォルト: 0.5）
        float iblIntensity;         ///< IBL強度（0=このマテリアルはIBL無効, デフォルト: 1.0）
    };

    static_assert(sizeof(MaterialConstants) % 16 == 0,
        "MaterialConstants must be 16-byte aligned for HLSL cbuffer");

}
