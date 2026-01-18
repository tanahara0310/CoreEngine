#pragma once

#include <d3d12.h>
#include <externals/DirectXTex/DirectXTex.h>
#include <string>
#include <wrl.h>
#include <unordered_map>
#include <mutex>

// 前方宣言
namespace CoreEngine {
	class DirectXCommon;
}


namespace CoreEngine
{
class TextureManager {
public:
	struct LoadedTexture {
		Microsoft::WRL::ComPtr<ID3D12Resource> texture;
		Microsoft::WRL::ComPtr<ID3D12Resource> intermediate;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
	};

	// シングルトンアクセス
	static TextureManager& GetInstance();

	// コピー・ムーブを禁止
	TextureManager(const TextureManager&) = delete;
	TextureManager& operator=(const TextureManager&) = delete;
	TextureManager(TextureManager&&) = delete;
	TextureManager& operator=(TextureManager&&) = delete;

	/// @brief 初期化処理
	/// @param dxCommon dxCommonへのポインタ
	void Initialize(CoreEngine::DirectXCommon* dxCommon);

	/// @brief テクスチャの読み込み
	/// @param filePath ファイルパス（Assetsフォルダを省略可能）
	/// @return 読み込まれたテクスチャ
	LoadedTexture Load(const std::string& filePath);

	/// @brief テクスチャのメタデータを取得
	/// @param filePath ファイルパス（Assetsフォルダを省略可能）
	/// @return テクスチャのメタデータ（幅・高さなど）
	DirectX::TexMetadata GetMetadata(const std::string& filePath);

	/// @brief 初期化済みかどうかを確認
	/// @return 初期化済みならtrue
	bool IsInitialized() const { return isInitialized_; }

	/// @brief 全てのテクスチャをクリア（デバッグ用）
	void Clear();

	/// @brief テクスチャキャッシュへの読み取り専用アクセス
	/// @return テクスチャキャッシュへの参照
	const std::unordered_map<std::string, LoadedTexture>& GetTextureCache() const { return textureCache_; }

private:
	// プライベートコンストラクタ・デストラクタ
	TextureManager() = default;
	~TextureManager() = default;

	/// @brief フルパスを解決（Assetsフォルダを自動的に追加）
	/// @param filePath 入力パス
	/// @return 解決されたフルパス
	std::string ResolveFilePath(const std::string& filePath) const;

	CoreEngine::DirectXCommon* dxCommon_ = nullptr;
	bool isInitialized_ = false;

	// デフォルトのベースパス
	const std::string basePath_ = "Assets/";

	// ファイルパスごとにテクスチャを保持
	std::unordered_map<std::string, LoadedTexture> textureCache_;
	// メタデータキャッシュ
	std::unordered_map<std::string, DirectX::TexMetadata> metadataCache_;

	// スレッドセーフ用ミューテックス
	mutable std::mutex cacheMutex_;
};
}
