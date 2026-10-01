#pragma once

#include <cstdint>

#include "Graphics/Shader/CBufferLayout.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine
{
    /// @brief ヒットシェーディングのインスタンス表の 1 行（TLAS のインスタンスと同じ並び）
    /// @details HLSL 側 Include/RayTracing/RTHitShading.hlsli の RTHitInstance と 1:1
    struct RTHitInstance {
        Vector4 objectToWorld[3];      ///< TLAS と同じ行優先 3×4
        uint32_t vertexBufferIndex;    ///< 頂点バッファ（ByteAddressBuffer）のヒープ内インデックス
        uint32_t indexBufferIndex;     ///< 索引バッファ（ByteAddressBuffer）のヒープ内インデックス
        uint32_t firstSubMesh;         ///< サブメッシュ表の先頭
        uint32_t subMeshCount;         ///< サブメッシュ数
    };
    static constexpr Cb::Field kRTHitInstanceFields[] = {
        CB_FIELD(RTHitInstance, objectToWorld), CB_FIELD(RTHitInstance, vertexBufferIndex),
        CB_FIELD(RTHitInstance, indexBufferIndex), CB_FIELD(RTHitInstance, firstSubMesh),
        CB_FIELD(RTHitInstance, subMeshCount),
    };
    CB_VERIFY_STRIDE(RTHitInstance, kRTHitInstanceFields);

    /// @brief ヒットシェーディングのサブメッシュ表の 1 行（材質を含む）
    /// @details HLSL 側 Include/RayTracing/RTHitShading.hlsli の RTHitSubMesh と 1:1
    struct RTHitSubMesh {
        uint32_t firstTriangle;          ///< 索引バッファ内の先頭の三角形
        uint32_t triangleCount;          ///< 三角形の数
        uint32_t baseColorTextureIndex;  ///< ベースカラーテクスチャのヒープ内インデックス（kRTHitNoTexture = 無し）
        uint32_t emissiveTextureIndex;   ///< エミッシブテクスチャのヒープ内インデックス（kRTHitNoTexture = 無し）
        Vector4 baseColor;               ///< ベースカラーファクター（rgba）
        Vector4 uvTransformU;            ///< 変換後の u = dot(xyz, (u, v, 1))
        Vector4 uvTransformV;            ///< 変換後の v = dot(xyz, (u, v, 1))
        Vector3 emissive;                ///< エミッシブファクター
        float metallic;                  ///< 金属性ファクター
        float roughness;                 ///< 粗さファクター
        float alphaCutoff;               ///< アルファがこれ以下の所は抜く
        uint32_t flags;                  ///< kRTHitSubMeshFlag*
        uint32_t metallicRoughnessTextureIndex; ///< 金属性（B）・粗さ（G）テクスチャのヒープ内インデックス（kRTHitNoTexture = 無し）
        uint32_t normalTextureIndex;     ///< 法線テクスチャのヒープ内インデックス（kRTHitNoTexture = 使わない）
        uint32_t occlusionTextureIndex;  ///< AO テクスチャ（R）のヒープ内インデックス（kRTHitNoTexture = 無し）
        float occlusionStrength;         ///< AO の強さ（0 = AO なし）
        float pad;
    };
    static constexpr Cb::Field kRTHitSubMeshFields[] = {
        CB_FIELD(RTHitSubMesh, firstTriangle), CB_FIELD(RTHitSubMesh, triangleCount),
        CB_FIELD(RTHitSubMesh, baseColorTextureIndex), CB_FIELD(RTHitSubMesh, emissiveTextureIndex),
        CB_FIELD(RTHitSubMesh, baseColor), CB_FIELD(RTHitSubMesh, uvTransformU),
        CB_FIELD(RTHitSubMesh, uvTransformV), CB_FIELD(RTHitSubMesh, emissive),
        CB_FIELD(RTHitSubMesh, metallic), CB_FIELD(RTHitSubMesh, roughness),
        CB_FIELD(RTHitSubMesh, alphaCutoff), CB_FIELD(RTHitSubMesh, flags),
        CB_FIELD(RTHitSubMesh, metallicRoughnessTextureIndex), CB_FIELD(RTHitSubMesh, normalTextureIndex),
        CB_FIELD(RTHitSubMesh, occlusionTextureIndex), CB_FIELD(RTHitSubMesh, occlusionStrength),
        CB_FIELD(RTHitSubMesh, pad),
    };
    CB_VERIFY_STRIDE(RTHitSubMesh, kRTHitSubMeshFields);

    /// @brief テクスチャが無いことを表すヒープ内インデックス
    inline constexpr uint32_t kRTHitNoTexture = 0xFFFFFFFFu;
    /// @brief ライティングする材質（0 ならアンリット）
    inline constexpr uint32_t kRTHitSubMeshFlagLit = 1u << 0;
    /// @brief ディザで抜く材質（しきい値 0.5 で抜く）
    inline constexpr uint32_t kRTHitSubMeshFlagDither = 1u << 1;
}
