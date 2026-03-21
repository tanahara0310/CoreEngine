#pragma once
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector4.h"
#include "Math/Vector/Vector3.h"
#include <cstdint>


namespace CoreEngine
{
    /// @brief GPU定数バッファに送信するマテリアルパラメータ（PBR専用）
    /// @note シェーダーとのメモリレイアウトを一致させる必要があります
    struct MaterialConstants {
        Vector4 color;              ///< ベースカラー (RGBA)
        int32_t enableLighting;     ///< 0=アンリット, 1=PBRライティング有効
        float padding[3];           ///< 16バイトアライメント用
        Matrix4x4 uvTransform;      ///< UVトランスフォーム行列

        // ===== PBR Parameters =====
        float metallic;             ///< 金属性 (0.0=非金属, 1.0=金属)
        float roughness;            ///< 粗さ (0.0=滑らか, 1.0=粗い)
        float ao;                   ///< Ambient Occlusion (0.0=完全遮蔽, 1.0=遮蔽なし)
        int32_t useNormalMap;       ///< 法線マップ使用フラグ

        int32_t useMetallicMap;     ///< メタリックマップ使用フラグ
        int32_t useRoughnessMap;    ///< ラフネスマップ使用フラグ
        int32_t useAOMap;           ///< AOマップ使用フラグ
        int32_t enableDithering;    ///< ディザリング有効フラグ (透明・葉など)
        
        // ===== IBL Parameters =====
        float ditheringScale;       ///< ディザリングスケール
        int32_t enableIBL;          ///< IBL有効フラグ
        float iblIntensity;         ///< IBL強度 (デフォルト: 1.0)
        float padding2;             ///< 16バイトアライメント用
    };

    static_assert(sizeof(MaterialConstants) % 16 == 0,
        "MaterialConstants must be 16-byte aligned for HLSL cbuffer");

}
