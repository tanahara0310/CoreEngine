#pragma once
#include <string>


namespace CoreEngine
{
/// @brief マテリアルデータを表す構造体（PBR対応）
struct MaterialData {
	std::string name;                        // マテリアル名
	std::string baseColorTexture;            // ベースカラー（Albedo）テクスチャ
	std::string metallicRoughnessTexture;    // MetallicRoughness統合テクスチャ
	std::string normalTexture;               // 法線マップ
	std::string occlusionTexture;            // アンビエントオクルージョンマップ
	std::string emissiveTexture;             // エミッシブ（発光）テクスチャ
	
	
	// 下位互換性のためのプロパティ（廃止予定）
	std::string textureFilePath;  // 注意：廃止予定。baseColorTextureを使用してください
};
}
