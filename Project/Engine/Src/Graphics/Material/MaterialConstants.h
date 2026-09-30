#pragma once
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector4.h"
#include "Math/Vector/Vector3.h"
#include <cstdint>


namespace CoreEngine
{
    /// @brief GPU 定数バッファに送信するマテリアルパラメータ（PBR 専用）
    /// @note glTF 準拠の「ファクター × テクスチャ」乗算方式。
    ///       テクスチャが無い場合は白 1x1 がバインドされるのでファクターがそのまま最終値になる。
    /// @warning Shaders/Include/Object/ObjectMaterialConstants.hlsli とメモリレイアウトを一致させること
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

        // ===== 頂点アニメーション（Shaders/Include/Object/VertexAnimation.hlsli） =====
        // 頂点ごとの値は VertexData::animData（glTF の TEXCOORD_1 / TEXCOORD_2）が持つ
        int32_t vertexAnimation;    ///< 種類（VertexAnimationType: 0=なし, 1=植物, 2=海草, 3=魚）
        float vertexAnimStrength;   ///< 振幅の倍率（1 = モデル作成時の想定。デフォルト: 1.0）
        float vertexAnimSpeed;      ///< 速さの倍率（1 = 既定の周期。デフォルト: 1.0）
        float vertexAnimPadding;    ///< 16 バイト境界合わせ（未使用）
    };

    static_assert(sizeof(MaterialConstants) % 16 == 0,
        "MaterialConstants must be 16-byte aligned for HLSL cbuffer");

    /// @brief 頂点アニメーションの種類（MaterialConstants::vertexAnimation の値）
    /// @note 値は HLSL 側 VertexAnimation.hlsli の kVertexAnim* と一致させること
    enum class VertexAnimationType : int32_t {
        None = 0,     ///< 動かさない
        Plant = 1,    ///< 植物: 風で株全体が曲がり、葉がしなり、葉先が震える
        Seagrass = 2, ///< 海草: 波の寄せ返しでゆっくり往復する
        Fish = 3,     ///< 魚: 体を波打たせ、胸びれで漕ぐ
    };

    /// HLSL 側 Shaders/Include/Object/ObjectMaterialConstants.hlsli の Material と 1:1
    static constexpr Cb::Field kMaterialConstantsFields[] = {
        CB_FIELD(MaterialConstants, color), CB_FIELD(MaterialConstants, uvTransform),
        CB_FIELD(MaterialConstants, metallic), CB_FIELD(MaterialConstants, roughness),
        CB_FIELD(MaterialConstants, occlusionStrength), CB_FIELD(MaterialConstants, useNormalMap),
        CB_FIELD(MaterialConstants, emissiveFactor), CB_FIELD(MaterialConstants, enableLighting),
        CB_FIELD(MaterialConstants, enableDithering), CB_FIELD(MaterialConstants, ditheringScale),
        CB_FIELD(MaterialConstants, alphaCutoff), CB_FIELD(MaterialConstants, iblIntensity),
        CB_FIELD(MaterialConstants, vertexAnimation), CB_FIELD(MaterialConstants, vertexAnimStrength),
        CB_FIELD(MaterialConstants, vertexAnimSpeed), CB_FIELD(MaterialConstants, vertexAnimPadding),
    };
    CB_VERIFY_LAYOUT(MaterialConstants, kMaterialConstantsFields);
    // HLSL 側の照合（CB_BIND_HLSL）は入れていない。"gMaterial" という変数名を
    // 3D オブジェクト用（本構造体・144B）とスプライト / UI 用（80B）が別レイアウトで共用しており、
    // 名前だけではどちらを指すか決められないため。
    // 照合を有効にしたい場合は HLSL 側の変数名を分けること。
}
