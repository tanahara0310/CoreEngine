#pragma once
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector4.h"
#include "Math/Vector/Vector3.h"
#include <cstdint>


namespace CoreEngine
{
    /// @brief マテリアルのシェーディングモード
    enum class ShadingMode : int32_t
    {
        PBR = 0,  ///< PBR ライティング（IBL なし、ハーフランバートアンビエント代替）
        PBR_IBL = 1,  ///< PBR + IBL ライティング
        Lambert = 2,  ///< 従来のランバートシェーディング（拡散反射のみ、スペキュラなし）
        HalfLambert = 3,  ///< 従来のハーフランバートシェーディング（拡散反射のみ、スペキュラなし）
    };

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

        // ===== Shading Mode =====
        float ditheringScale;       ///< ディザリングスケール
        int32_t shadingMode;        ///< シェーディングモード（ShadingMode 列挙型）
        float iblIntensity;         ///< IBL強度 (ShadingMode::PBR_IBL 時に使用, デフォルト: 1.0)
        float alphaCutoff;          ///< discard 判定に使用するアルファしきい値（デフォルト: 0.5）
    };

    static_assert(sizeof(MaterialConstants) % 16 == 0,
        "MaterialConstants must be 16-byte aligned for HLSL cbuffer");

}
