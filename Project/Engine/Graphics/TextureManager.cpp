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
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <chrono>

using namespace Microsoft::WRL;


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

        std::wstring filePathW = Logger::GetInstance().ConvertString(resolvedPath);
        std::wstring extension = std::filesystem::path(filePathW).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);

        bool isDDS = (extension == L".dds");
        bool isHDR = (extension == L".hdr");
        std::string ddsPath;

        // HDRファイルの場合、キューブマップDDSに変換
        if (isHDR && ddsGenerationEnabled_) {
            std::string cubemapDDSPath = GetCubemapDDSPath(resolvedPath);
            std::wstring cubemapDDSPathW = Logger::GetInstance().ConvertString(cubemapDDSPath);

            // キューブマップDDSが存在しない場合は生成
            if (!std::filesystem::exists(cubemapDDSPathW)) {
                Logger::GetInstance().Log(std::format("Generating cubemap DDS from HDR: {}", resolvedPath), LogLevel::INFO, LogCategory::Graphics);
                if (GenerateCubemapFromHDR(resolvedPath, cubemapDDSPath)) {
                    // 生成成功、キューブマップDDSから読み込み
                    filePathW = cubemapDDSPathW;
                    resolvedPath = cubemapDDSPath;
                    isDDS = true;
                    isHDR = false;
                } else {
                    Logger::GetInstance().Log(std::format("Failed to generate cubemap, loading HDR as 2D texture: {}", resolvedPath), LogLevel::WARNING, LogCategory::Graphics);
                }
            } else {
                // キューブマップDDSが既に存在する場合はそれを使用
                Logger::GetInstance().Log(std::format("Loading from cubemap DDS cache: {}", cubemapDDSPath), LogLevel::INFO, LogCategory::Graphics);
                filePathW = cubemapDDSPathW;
                resolvedPath = cubemapDDSPath;
                isDDS = true;
                isHDR = false;
            }
        }

        // DDSキャッシュを使用する場合（DDS、HDR以外でDDS生成が有効）
        if (!isDDS && !isHDR && ddsGenerationEnabled_) {
            ddsPath = GetDDSCachePath(resolvedPath);
            std::wstring ddsPathW = Logger::GetInstance().ConvertString(ddsPath);

            // DDSキャッシュが存在するかチェック
            if (std::filesystem::exists(ddsPathW)) {
                Logger::GetInstance().Log(std::format("Loading from DDS cache: {}", ddsPath), LogLevel::INFO, LogCategory::Graphics);
                filePathW = ddsPathW;
                resolvedPath = ddsPath;
                isDDS = true;
            }
        }

        // 1. ファイル形式を自動判定してテクスチャを読み込み
        DirectX::ScratchImage image;
        HRESULT hr;

        if (isDDS) {
            hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
        } else if (isHDR) {
            hr = DirectX::LoadFromHDRFile(filePathW.c_str(), nullptr, image);
        } else {
            hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);

            // DDS生成が有効な場合は、読み込み成功後にDDSを生成
            if (SUCCEEDED(hr) && ddsGenerationEnabled_ && !ddsPath.empty()) {
                GenerateDDSCache(resolvedPath, ddsPath);
            }
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
        std::wstring extension = std::filesystem::path(filePathW).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);

        HRESULT hr;
        if (extension == L".dds") {
            hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
        } else if (extension == L".hdr") {
            hr = DirectX::LoadFromHDRFile(filePathW.c_str(), nullptr, image);
        } else {
            hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
        }

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

    std::string TextureManager::GetDDSCachePath(const std::string& originalPath) const
    {
        // 元のファイルと同じディレクトリに、拡張子を.ddsに変更して保存
        std::filesystem::path path(originalPath);
        std::filesystem::path parentPath = path.parent_path();
        std::string fileName = path.stem().string(); // 拡張子なしのファイル名

        // 親ディレクトリ + ファイル名 + .dds
        if (parentPath.empty()) {
            return fileName + ".dds";
        } else {
            return (parentPath / (fileName + ".dds")).string();
        }
    }

    std::string TextureManager::GetCubemapDDSPath(const std::string& originalPath) const
    {
        // HDRファイルのキューブマップDDSパスを生成（ファイル名_cubemap.dds）
        std::filesystem::path path(originalPath);
        std::filesystem::path parentPath = path.parent_path();
        std::string fileName = path.stem().string();

        if (parentPath.empty()) {
            return fileName + "_cubemap.dds";
        } else {
            return (parentPath / (fileName + "_cubemap.dds")).string();
        }
    }

    bool TextureManager::GenerateDDSCache(const std::string& sourcePath, const std::string& ddsPath)
    {
        try {
            // ソース画像を読み込み（ファイル形式を自動判定）
            DirectX::ScratchImage sourceImage;
            std::wstring sourcePathW = Logger::GetInstance().ConvertString(sourcePath);
            std::wstring extension = std::filesystem::path(sourcePathW).extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);

            HRESULT hr;
            if (extension == L".hdr") {
                hr = DirectX::LoadFromHDRFile(sourcePathW.c_str(), nullptr, sourceImage);
            } else {
                hr = DirectX::LoadFromWICFile(sourcePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, sourceImage);
            }

            if (FAILED(hr)) {
                Logger::GetInstance().Log(std::format("Failed to load source for DDS generation: {}", sourcePath), LogLevel::WARNING, LogCategory::Graphics);
                return false;
            }

            // ミップマップ生成
            DirectX::ScratchImage mipChain;
            const DirectX::TexMetadata& metadata = sourceImage.GetMetadata();

            if (metadata.width == 1 && metadata.height == 1) {
                mipChain = std::move(sourceImage);
            } else {
                hr = DirectX::GenerateMipMaps(
                    sourceImage.GetImages(),
                    sourceImage.GetImageCount(),
                    sourceImage.GetMetadata(),
                    DirectX::TEX_FILTER_SRGB,
                    0,
                    mipChain
                );

                if (FAILED(hr)) {
                    Logger::GetInstance().Log(std::format("Failed to generate mipmaps for DDS: {}", sourcePath), LogLevel::WARNING, LogCategory::Graphics);
                    mipChain = std::move(sourceImage);
                }
            }

            // BC3圧縮（SRGB対応）
            DirectX::ScratchImage compressedImage;
            hr = DirectX::Compress(
                mipChain.GetImages(),
                mipChain.GetImageCount(),
                mipChain.GetMetadata(),
                DXGI_FORMAT_BC3_UNORM_SRGB,
                DirectX::TEX_COMPRESS_PARALLEL,
                DirectX::TEX_THRESHOLD_DEFAULT,
                compressedImage
            );

            DirectX::ScratchImage* imageToSave = &mipChain;
            if (SUCCEEDED(hr)) {
                imageToSave = &compressedImage;
            } else {
                Logger::GetInstance().Log(std::format("Failed to compress texture, saving uncompressed DDS: {}", sourcePath), LogLevel::WARNING, LogCategory::Graphics);
            }

            // DDSファイルとして保存
            std::wstring ddsPathW = Logger::GetInstance().ConvertString(ddsPath);
            hr = DirectX::SaveToDDSFile(
                imageToSave->GetImages(),
                imageToSave->GetImageCount(),
                imageToSave->GetMetadata(),
                DirectX::DDS_FLAGS_NONE,
                ddsPathW.c_str()
            );

            if (SUCCEEDED(hr)) {
                Logger::GetInstance().Log(std::format("DDS cache generated: {}", ddsPath), LogLevel::INFO, LogCategory::Graphics);
                return true;
            } else {
                Logger::GetInstance().Log(std::format("Failed to save DDS cache: {}", ddsPath), LogLevel::WARNING, LogCategory::Graphics);
                return false;
            }

        }
        catch (const std::exception& e) {
            Logger::GetInstance().Log(std::format("Exception during DDS generation: {}", e.what()), LogLevel::WARNING, LogCategory::Graphics);
            return false;
        }
    }

    bool TextureManager::GenerateCubemapFromHDR(const std::string& hdrPath, const std::string& cubemapDDSPath)
    {
        try {
            // cmftのパスを構築
            std::filesystem::path cmftPath = "externals/cmft/cmftRelease.exe";
            
            // cmftが存在しない場合は、ビルド済みの場所を探す
            if (!std::filesystem::exists(cmftPath)) {
                cmftPath = "externals/cmft/_build/win64_vs2015/bin/cmftRelease.exe";
            }
            if (!std::filesystem::exists(cmftPath)) {
                Logger::GetInstance().Log("cmft executable not found. Please build cmft first.", LogLevel::WARNING, LogCategory::Graphics);
                return false;
            }

			// パスを絶対パスに変換（カレントディレクトリ依存を回避）
			std::filesystem::path hdrPathFS = std::filesystem::absolute(hdrPath);
			std::filesystem::path outputPathFS = std::filesystem::absolute(cubemapDDSPath);
			std::filesystem::path outputBase = outputPathFS.parent_path() / outputPathFS.stem();

			// cmftの絶対パスも取得
			std::filesystem::path cmftPathAbs = std::filesystem::absolute(cmftPath);

			// パスを文字列に変換し、バックスラッシュをスラッシュに変換（cmft互換）
			auto pathToUnixStyle = [](const std::filesystem::path& p) -> std::string {
				std::string str = p.string();
				std::replace(str.begin(), str.end(), '\\', '/');
				return str;
			};

			std::string cmftPathStr = pathToUnixStyle(cmftPathAbs);
			std::string hdrPathStr = pathToUnixStyle(hdrPathFS);
			std::string outputBaseStr = pathToUnixStyle(outputBase);

			Logger::GetInstance().Log(std::format("cmft path: {}", cmftPathStr), LogLevel::INFO, LogCategory::Graphics);
			Logger::GetInstance().Log(std::format("HDR input path: {}", hdrPathStr), LogLevel::INFO, LogCategory::Graphics);
			Logger::GetInstance().Log(std::format("DDS output base: {}", outputBaseStr), LogLevel::INFO, LogCategory::Graphics);

			// cmftコマンドを構築（元のHDR解像度を使用）
			std::string command = std::format(
				"\"{}\" --input \"{}\" --output0 \"{}\" --output0params dds,rgba16f,cubemap",
				cmftPathStr,
				hdrPathStr,
				outputBaseStr
			);

			Logger::GetInstance().Log(std::format("Executing cmft: {}", command), LogLevel::INFO, LogCategory::Graphics);

			// コマンドをCreateProcessで実行（std::system()より信頼性が高い）
			STARTUPINFOA si = {};
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESHOWWINDOW;
			si.wShowWindow = SW_HIDE; // ウィンドウを非表示

			PROCESS_INFORMATION pi = {};

			// コマンドライン文字列を書き込み可能なバッファにコピー
			std::string cmdLine = command;
			std::vector<char> cmdBuffer(cmdLine.begin(), cmdLine.end());
			cmdBuffer.push_back('\0');

			BOOL result = CreateProcessA(
				nullptr,                // アプリケーション名（コマンドラインから取得）
				cmdBuffer.data(),       // コマンドライン
				nullptr,                // プロセスハンドル継承なし
				nullptr,                // スレッドハンドル継承なし
				FALSE,                  // ハンドル継承なし
				0,                      // 作成フラグなし
				nullptr,                // 親の環境変数を使用
				nullptr,                // 親のカレントディレクトリを使用
				&si,                    // STARTUPINFO
				&pi                     // PROCESS_INFORMATION
			);

			int exitCode = 1; // デフォルトは失敗
			if (result) {
				// プロセス完了を待機
				WaitForSingleObject(pi.hProcess, INFINITE);
				
				// 終了コードを取得
				DWORD dwExitCode = 0;
				GetExitCodeProcess(pi.hProcess, &dwExitCode);
				exitCode = static_cast<int>(dwExitCode);

				// ハンドルをクローズ
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);
			} else {
				DWORD error = GetLastError();
				Logger::GetInstance().Log(std::format("Failed to create process: GetLastError={}", error), LogLevel::WARNING, LogCategory::Graphics);
			}

			// 実行後に少し待機（ファイルシステムの同期待ち）
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

			// 結果を確認
			bool fileExists = std::filesystem::exists(cubemapDDSPath);
			
			Logger::GetInstance().Log(std::format("cmft result: exit code={}, output file exists={}", 
				exitCode, fileExists), LogLevel::INFO, LogCategory::Graphics);

			if (fileExists) {
				// ファイルサイズも確認
				auto fileSize = std::filesystem::file_size(cubemapDDSPath);
				Logger::GetInstance().Log(std::format("Cubemap DDS generated: {} (size: {} bytes)", 
					cubemapDDSPath, fileSize), LogLevel::INFO, LogCategory::Graphics);
				return true;
			} else {
				Logger::GetInstance().Log(std::format("Failed to generate cubemap DDS: exit code={}, file not found: {}", 
					exitCode, cubemapDDSPath), LogLevel::WARNING, LogCategory::Graphics);
				return false;
			}

        }
        catch (const std::exception& e) {
            Logger::GetInstance().Log(std::format("Exception during cubemap generation: {}", e.what()), LogLevel::WARNING, LogCategory::Graphics);
            return false;
        }
    }
}