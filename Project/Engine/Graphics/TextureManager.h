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


namespace CoreEngine {

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

		/// @brief DDS自動生成を有効化/無効化
		/// @param enable 有効化する場合true
		void SetDDSCacheEnabled(bool enable) { ddsGenerationEnabled_ = enable; }

		/// @brief DDS自動生成が有効かどうか
		/// @return 有効ならtrue
		bool IsDDSCacheEnabled() const { return ddsGenerationEnabled_; }

	private:
		// プライベートコンストラクタ・デストラクタ
		TextureManager() = default;
		~TextureManager() = default;

		/// @brief フルパスを解決（Assetsフォルダを自動的に追加）
		/// @param filePath 入力パス
		/// @return 解決されたフルパス
		std::string ResolveFilePath(const std::string& filePath) const;

		/// @brief DDSファイルパスを生成
		/// @param originalPath 元のファイルパス
		/// @return DDSキャッシュファイルパス
		std::string GetDDSCachePath(const std::string& originalPath) const;

		/// @brief キューブマップDDSファイルパスを生成（HDR用）
		/// @param originalPath 元のファイルパス
		/// @return キューブマップDDSファイルパス
		std::string GetCubemapDDSPath(const std::string& originalPath) const;

		/// @brief PNGからDDSを生成して保存
		/// @param sourcePath PNGファイルパス
		/// @param ddsPath 保存先DDSファイルパス
		/// @return 成功したらtrue
		bool GenerateDDSCache(const std::string& sourcePath, const std::string& ddsPath);

		/// @brief HDRをキューブマップDDSに変換（cmft使用）
		/// @param hdrPath HDRファイルパス
		/// @param cubemapDDSPath 出力キューブマップDDSパス
		/// @return 成功したらtrue
		bool GenerateCubemapFromHDR(const std::string& hdrPath, const std::string& cubemapDDSPath);

		CoreEngine::DirectXCommon* dxCommon_ = nullptr;
		bool isInitialized_ = false;
		bool ddsGenerationEnabled_ = true; // DDS自動生成を有効化（デフォルト有効）

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
