#pragma once
#include <string>

#include "Graphics/Material/MaterialConstants.h" // VertexAnimationType
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"


namespace CoreEngine
{
    /// @brief マテリアルのアセットデータ（ファイルから読み込むメタデータ）
    /// @note glTF PBR Metallic-Roughness 準拠。テクスチャパスとファクター値を保持し、
    ///       実行時の MaterialInstance の初期化に使用される。
    ///       ファクターは対応するテクスチャと乗算合成される（glTF 仕様）。
    struct MaterialAsset {
        std::string name;                        // マテリアル名

        // ===== テクスチャパス =====
        std::string baseColorTexture;            // ベースカラー（Albedo）テクスチャ
        std::string metallicRoughnessTexture;    // MetallicRoughness統合テクスチャ (G=Roughness, B=Metallic)
        std::string normalTexture;               // 法線マップ
        std::string occlusionTexture;            // アンビエントオクルージョンマップ
        std::string emissiveTexture;             // エミッシブ（発光）テクスチャ

        // ===== PBR ファクター =====
        // glTF はファクターを明示的に持つ（省略時は spec デフォルト値を assimp が補完する）。
        // OBJ/FBX などファクターを持たない形式ではここの初期値がそのまま使われる。
        Vector4 baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f }; // ベースカラーファクター
        float metallicFactor = 0.0f;                          // 金属性ファクター（非PBR形式向けに非金属デフォルト）
        float roughnessFactor = 0.5f;                         // 粗さファクター（非PBR形式向けに中間デフォルト）
        Vector3 emissiveFactor = { 0.0f, 0.0f, 0.0f };        // エミッシブファクター
        float alphaCutoff = 0.5f;                             // アルファカットオフしきい値

        // ===== 頂点アニメーション（glTF のマテリアルの extras。ModelLoader::ApplyGltfMaterialExtras） =====
        // モデル側で「置くだけで揺れる・泳ぐ」ようにするための既定値。MaterialComponent で個別に上書きできる。
        VertexAnimationType vertexAnimation = VertexAnimationType::None; // 種類
        float vertexAnimStrength = 1.0f;                                 // 振幅の倍率
        float vertexAnimSpeed = 1.0f;                                    // 速さの倍率
    };


}
