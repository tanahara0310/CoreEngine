#pragma once
#include <string>


namespace CoreEngine
{
    /// @brief マテリアルのアセットデータ（ファイルから読み込むメタデータ）
    /// @note ファイルパスやデフォルト値などを保持し、実行時のMaterialクラスの初期化に使用されます
    struct MaterialAsset {
        std::string name;                        // マテリアル名
        std::string baseColorTexture;            // ベースカラー（Albedo）テクスチャ
        std::string metallicRoughnessTexture;    // MetallicRoughness統合テクスチャ
        std::string normalTexture;               // 法線マップ
        std::string occlusionTexture;            // アンビエントオクルージョンマップ
        std::string emissiveTexture;             // エミッシブ（発光）テクスチャ
    };


}
