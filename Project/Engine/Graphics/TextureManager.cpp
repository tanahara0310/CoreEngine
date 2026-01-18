#include "TextureManager.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Utility/Logger/Logger.h"
#include "Engine/Utility/FileErrorDialog/FileErrorDialog.h"

#include "externals/DirectXTex/d3dx12.h"
#include <Windows.h>
#include <vector>
#include <cassert>
#include <stdexcept>
#include <format>

using namespace Microsoft::WRL;

// シングルトンインスタンス取得

namespace CoreEngine
{
TextureManager& TextureManager::GetInstance()
{
	static TextureManager instance;
	return instance;
}

// 初期化
void TextureManager::Initialize(CoreEngine::DirectXCommon* dxCommon)
{
	std::lock_guard<std::mutex> lock(cacheMutex_);

	assert(dxCommon != nullptr);
	dxCommon_ = dxCommon;
	isInitialized_ = true;
}

// テクスチャの読み込み
TextureManager::LoadedTexture TextureManager::Load(const std::string& filePath)
{
	std::lock_guard<std::mutex> lock(cacheMutex_);

	assert(isInitialized_ && "TextureManager is not initialized!");

	// パスを解決
	std::string resolvedPath = ResolveFilePath(filePath);

	// すでに読み込んでいるならキャッシュを返す
	auto it = textureCache_.find(resolvedPath);
	if (it != textureCache_.end()) {
		Logger::GetInstance().Log(std::format("Texture already loaded (cache hit): {}", resolvedPath), LogLevel::INFO, LogCategory::Graphics);
		return it->second;
	}

	// ロード開始ログ
	Logger::GetInstance().Log(std::format("Loading texture: {}", resolvedPath), LogLevel::INFO, LogCategory::Graphics);

	LoadedTexture result{};

	// 1. テクスチャの読み込みとミップマップ生成
	DirectX::ScratchImage image;
	std::wstring filePathW = Logger::GetInstance().ConvertString(resolvedPath);

	HRESULT hr;
	if (filePathW.ends_with(L".dds")) { // DDSファイルの場合
		hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);

	} else { // その他の形式の場合

		hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	}

	if (FAILED(hr)) {
		std::string errorMsg = std::format("Failed to load texture file: {}\nHRESULT: 0x{:08X}\nPlease check if the file exists and the path is correct.", resolvedPath, static_cast<unsigned int>(hr));
		Logger::GetInstance().Log(errorMsg, LogLevel::Error, LogCategory::Graphics);
		FileErrorDialog::ShowTextureError("Failed to load texture file", resolvedPath, hr);
		throw std::runtime_error(errorMsg);
	}

	DirectX::ScratchImage mipImages;

	if (DirectX::IsCompressed(image.GetMetadata().format)) { //圧縮フォーマットか調べる
		mipImages = std::move(image); //圧縮フォーマットの場合はミップマップ生成せずそのまま使う

	} else {
		// 画像サイズが1x1の場合はミップマップ生成をスキップ
		const DirectX::TexMetadata& metadata = image.GetMetadata();
		if (metadata.width == 1 && metadata.height == 1) {
			// 1x1テクスチャの場合はそのまま使用
			mipImages = std::move(image);
		} else {

			size_t mipLevels = 0; // 0を指定すると自動的に最大ミップレベルを計算
			
			hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, mipLevels, mipImages);
			
			if (FAILED(hr)) {
				std::string errorMsg = std::format("Failed to generate mipmaps for texture: {}\nHRESULT: 0x{:08X}", resolvedPath, static_cast<unsigned int>(hr));
				Logger::GetInstance().Log(errorMsg, LogLevel::Error, LogCategory::Graphics);
				FileErrorDialog::ShowTextureError("Failed to generate mipmaps for texture", resolvedPath, hr);
				throw std::runtime_error(errorMsg);
			}
		}
	}

	const DirectX::TexMetadata& texMetadata = mipImages.GetMetadata();

	// メタデータをキャッシュに保存
	metadataCache_[resolvedPath] = texMetadata;

	// 2. リソース生成
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(texMetadata.width);
	resourceDesc.Height = UINT(texMetadata.height);
	resourceDesc.MipLevels = UINT16(texMetadata.mipLevels);
	resourceDesc.DepthOrArraySize = UINT16(texMetadata.arraySize);
	resourceDesc.Format = texMetadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(texMetadata.dimension);

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	hr = dxCommon_->GetDevice()->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&result.texture));
	if (FAILED(hr)) {
		std::string errorMsg = "Failed to create texture resource: " + resolvedPath;
		Logger::GetInstance().Log(errorMsg, LogLevel::Error, LogCategory::Graphics);
		FileErrorDialog::ShowTextureError("Failed to create texture resource", resolvedPath, hr);
		throw std::runtime_error(errorMsg);
	}

	// 3. アップロードリソースの作成とデータ転送
	std::vector<D3D12_SUBRESOURCE_DATA> subResources;
	DirectX::PrepareUpload(dxCommon_->GetDevice(), mipImages.GetImages(), mipImages.GetImageCount(), texMetadata, subResources);

	uint64_t intermediateSize = GetRequiredIntermediateSize(result.texture.Get(), 0, UINT(subResources.size()));
	result.intermediate = ResourceFactory::CreateBufferResource(dxCommon_->GetDevice(), intermediateSize);

	UpdateSubresources(dxCommon_->GetCommandList(), result.texture.Get(), result.intermediate.Get(), 0, 0, UINT(subResources.size()), subResources.data());

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = result.texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	dxCommon_->GetCommandList()->ResourceBarrier(1, &barrier);

	// 4. SRV作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = texMetadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	if (texMetadata.IsCubemap()) {
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.TextureCube.MostDetailedMip = 0; //unionがTextureCubeになったが、内部パラメータの意味はTexture2dと変わらず
		srvDesc.TextureCube.MipLevels = UINT_MAX;
		srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

	} else { //通常の2Dテクスチャ

		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = UINT(texMetadata.mipLevels);

	}

	// DescriptorManager経由でSRVを作成
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
	dxCommon_->GetDescriptorManager()->CreateSRV(
		result.texture.Get(),
		srvDesc,
		cpuHandle,
		gpuHandle,
		resolvedPath);

	// 結果を LoadedTexture にセット
	result.cpuHandle = cpuHandle;
	result.gpuHandle = gpuHandle;

	// 最後にキャッシュに保存
	textureCache_[resolvedPath] = result;

	return result;
}

DirectX::TexMetadata TextureManager::GetMetadata(const std::string& filePath)
{
	std::lock_guard<std::mutex> lock(cacheMutex_);

	assert(isInitialized_ && "TextureManager is not initialized!");

	// パスを解決
	std::string resolvedPath = ResolveFilePath(filePath);

	// キャッシュから検索
	auto it = metadataCache_.find(resolvedPath);
	if (it != metadataCache_.end()) {
		return it->second;
	}

	// キャッシュにない場合は画像を読み込んでメタデータを取得
	DirectX::ScratchImage image;
	std::wstring filePathW = Logger::GetInstance().ConvertString(resolvedPath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);

	if (FAILED(hr)) {
		std::string errorMsg = std::format("Failed to load texture file: {}\nHRESULT: 0x{:08X}\nPlease check if the file exists and the path is correct.", resolvedPath, static_cast<unsigned int>(hr));
		Logger::GetInstance().Log(errorMsg, LogLevel::Error, LogCategory::Graphics);
		FileErrorDialog::ShowTextureError("Failed to load texture file", resolvedPath, hr);
		throw std::runtime_error(errorMsg);
	}
	const DirectX::TexMetadata& texMetadata = image.GetMetadata();


	// キャッシュに保存
	metadataCache_[resolvedPath] = texMetadata;

	return texMetadata;
}

void TextureManager::Clear()
{
	std::lock_guard<std::mutex> lock(cacheMutex_);

	textureCache_.clear();
	metadataCache_.clear();
}

std::string TextureManager::ResolveFilePath(const std::string& filePath) const
{
	// すでにAssetsで始まっている場合はそのまま返す
	if (filePath.starts_with("Assets/") || filePath.starts_with("Assets\\")) {
		return filePath;
	}

	// 絶対パス（C:/ など）の場合はそのまま返す
	if (filePath.length() >= 2 && filePath[1] == ':') {
		return filePath;
	}

	// それ以外の場合はbasePath_を前に追加
	return basePath_ + filePath;
}
}
