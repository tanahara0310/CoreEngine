#include "TextureManager.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Utility/Logger/Logger.h"
#include "Engine/Utility/FileErrorDialog/FileErrorDialog.h"
#include "Engine/Utility/ProcessExecutor/ProcessExecutor.h"

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
        
        // キャッシュキーとして元のresolvedPathを保存（DDSに変換される前）
        std::string cacheKey = resolvedPath;

        // すでに読み込んでいるならキャッシュを返す
        auto it = textureCache_.find(cacheKey);
        if (it != textureCache_.end()) {
            Logger::GetInstance().Log(std::format("Texture already loaded (cache hit): {}", cacheKey), LogLevel::INFO, LogCategory::Graphics);
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

            bool needsRegeneration = false;

            // キューブマップDDSが存在しない場合は生成
            if (!std::filesystem::exists(cubemapDDSPathW)) {
                needsRegeneration = true;
            } else {
                // 存在する場合、タイムスタンプを比較
                auto sourceTime = std::filesystem::last_write_time(filePathW);
                auto ddsTime = std::filesystem::last_write_time(cubemapDDSPathW);

                if (sourceTime > ddsTime) {
                    Logger::GetInstance().Log(std::format("HDR source file is newer than cubemap DDS, regenerating: {}", resolvedPath), LogLevel::INFO, LogCategory::Graphics);
                    needsRegeneration = true;
                    // 古いDDSを削除
                    std::filesystem::remove(cubemapDDSPathW);
                }
            }

            if (needsRegeneration) {
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
                // キューブマップDDSが既に存在し、最新の場合はそれを使用
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
                // 元ファイルとDDSファイルのタイムスタンプを比較
                auto sourceTime = std::filesystem::last_write_time(filePathW);
                auto ddsTime = std::filesystem::last_write_time(ddsPathW);

                if (sourceTime > ddsTime) {
                    // 元ファイルの方が新しい場合は、DDSを再生成
                    Logger::GetInstance().Log(std::format("Source file is newer than DDS cache, regenerating: {}", resolvedPath), LogLevel::INFO, LogCategory::Graphics);
                    // 古いDDSを削除
                    std::filesystem::remove(ddsPathW);
                } else {
                    // DDSの方が新しいか同じ場合は、DDSから読み込み
                    Logger::GetInstance().Log(std::format("Loading from DDS cache: {}", ddsPath), LogLevel::INFO, LogCategory::Graphics);
                    filePathW = ddsPathW;
                    resolvedPath = ddsPath;
                    isDDS = true;
                }
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

        // 最後にキャッシュに保存（元のキーで保存）
        textureCache_[cacheKey] = result;

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
        std::string resolvedPath;

        // すでにAssetsで始まっている場合はそのまま使用
        if (filePath.starts_with("Assets/") || filePath.starts_with("Assets\\")) {
            resolvedPath = filePath;
        }
        // 絶対パス（C:/ など）の場合はそのまま使用
        else if (filePath.length() >= 2 && filePath[1] == ':') {
            resolvedPath = filePath;
        }
        // それ以外の場合はbasePath_を前に追加
        else {
            resolvedPath = basePath_ + filePath;
        }

        // パスの正規化：バックスラッシュをスラッシュに統一
        std::replace(resolvedPath.begin(), resolvedPath.end(), '\\', '/');

        return resolvedPath;
    }

    std::string TextureManager::GetDDSCachePath(const std::string& originalPath) const
    {
        // 元のファイルと同じディレクトリに、拡張子を.ddsに変更して保存
        std::filesystem::path path(originalPath);
        std::filesystem::path parentPath = path.parent_path();
        std::string fileName = path.stem().string(); // 拡張子なしのファイル名

        // 親ディレクトリ + ファイル名 + .dds
        std::string result;
        if (parentPath.empty()) {
            result = fileName + ".dds";
        } else {
            result = (parentPath / (fileName + ".dds")).string();
        }

        // パスの正規化：バックスラッシュをスラッシュに統一
        std::replace(result.begin(), result.end(), '\\', '/');

        return result;
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

    // ========================================
    // ヘルパー関数実装
    // ========================================

    std::filesystem::path TextureManager::FindCmftExecutable() const
    {
        std::filesystem::path cmftPath = cmtfRelativePath_;

        // 相対パスで存在チェック
        if (std::filesystem::exists(cmftPath))
        {
            return cmftPath;
        }

        // ビルドパスで存在チェック
        cmftPath = cmtfBuildPath_;
        if (std::filesystem::exists(cmftPath))
        {
            return cmftPath;
        }

        // 見つからない場合は空のパスを返す
        return std::filesystem::path();
    }

    std::string TextureManager::ConvertToUnixPath(const std::filesystem::path& path)
    {
        std::string str = path.string();
        std::replace(str.begin(), str.end(), '\\', '/');
        return str;
    }

    bool TextureManager::ValidateGeneratedCubemap(const std::string& filePath) const
    {
        if (!std::filesystem::exists(filePath))
        {
            return false;
        }

        try
        {
            auto fileSize = std::filesystem::file_size(filePath);
            if (fileSize == 0)
            {
                Logger::GetInstance().Log(std::format("Cubemap file is empty: {}", filePath), LogLevel::WARNING, LogCategory::Graphics);
                return false;
            }

            Logger::GetInstance().Log(std::format("Cubemap DDS generated: {} (size: {} bytes)", filePath, fileSize), LogLevel::INFO, LogCategory::Graphics);
            return true;
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            Logger::GetInstance().Log(std::format("Failed to validate cubemap file: {}", e.what()), LogLevel::WARNING, LogCategory::Graphics);
            return false;
        }
    }

    // ========================================
    // キューブマップ生成
    // ========================================

    bool TextureManager::GenerateCubemapFromHDR(const std::string& hdrPath, const std::string& cubemapDDSPath)
    {
        try {
            // cmft実行ファイルを検索
            std::filesystem::path cmftPath = FindCmftExecutable();
            if (cmftPath.empty())
            {
                Logger::GetInstance().Log("cmft executable not found. Please build cmft first.", LogLevel::WARNING, LogCategory::Graphics);
                return false;
            }

            // パスを絶対パスに変換
            std::filesystem::path hdrPathAbs = std::filesystem::absolute(hdrPath);
            std::filesystem::path outputPathAbs = std::filesystem::absolute(cubemapDDSPath);
            std::filesystem::path outputBase = outputPathAbs.parent_path() / outputPathAbs.stem();

            // cmftの絶対パスも取得
            std::filesystem::path cmftPathAbs = std::filesystem::absolute(cmftPath);

            // Unix形式のパスに変換（cmft互換）
            std::string cmftPathStr = ConvertToUnixPath(cmftPathAbs);
            std::string hdrPathStr = ConvertToUnixPath(hdrPathAbs);
            std::string outputBaseStr = ConvertToUnixPath(outputBase);

            Logger::GetInstance().Log(std::format("cmft path: {}", cmftPathStr), LogLevel::INFO, LogCategory::Graphics);
            Logger::GetInstance().Log(std::format("HDR input: {}", hdrPathStr), LogLevel::INFO, LogCategory::Graphics);
            Logger::GetInstance().Log(std::format("Output base: {}", outputBaseStr), LogLevel::INFO, LogCategory::Graphics);

            // cmftコマンドライン構築
            std::string command = std::format(
                "\"{}\" --input \"{}\" --output0 \"{}\" --output0params dds,rgba16f,cubemap",
                cmftPathStr,
                hdrPathStr,
                outputBaseStr
            );

            Logger::GetInstance().Log(std::format("Executing: {}", command), LogLevel::INFO, LogCategory::Graphics);

            // プロセス実行
            auto result = ProcessExecutor::Execute(command);

            if (!result.success)
            {
                Logger::GetInstance().Log(std::format("Failed to execute cmft: GetLastError={}", result.lastError), LogLevel::WARNING, LogCategory::Graphics);
                return false;
            }

            // ファイルシステムの同期待ち
            std::this_thread::sleep_for(std::chrono::milliseconds(processWaitTimeOutMs_));

            // 生成されたファイルを検証
            bool isValid = ValidateGeneratedCubemap(cubemapDDSPath);

            Logger::GetInstance().Log(std::format("cmft exit code: {}, validation: {}", result.exitCode, isValid ? "OK" : "FAILED"), LogLevel::INFO, LogCategory::Graphics);

            return isValid;

        }
        catch (const std::exception& e) {
            Logger::GetInstance().Log(std::format("Exception during cubemap generation: {}", e.what()), LogLevel::WARNING, LogCategory::Graphics);
            return false;
        }
    }
}
