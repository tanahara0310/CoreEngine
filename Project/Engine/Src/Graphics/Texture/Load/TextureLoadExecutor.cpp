#include "pch.h"
#include "TextureLoadExecutor.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Texture/Load/TextureImageProcessor.h"
#include "Graphics/Texture/Gpu/TextureGpuUploader.h"
#include "Utility/Logger/Logger.h"

#include <format>
#include <stdexcept>

namespace CoreEngine
{
    TextureLoadExecutor::ExecutionResult TextureLoadExecutor::Execute(
        CoreEngine::DirectXCommon* dxCommon,
        const std::string& resolvedPath,
        bool ddsGenerationEnabled,
        const std::string& ddsPath,
        const std::function<bool(const std::string&, const std::string&)>& ddsCacheGenerator)
    {
        // 読み込み対象パスをワイド文字列化し、DirectXTexのI/Oに渡す。
        std::wstring filePathW = Logger::GetInstance().ConvertString(resolvedPath);

        // 画像データを読み込み、必要であれば後続でDDS生成も実行する。
        DirectX::ScratchImage image;
        HRESULT hr = TextureImageProcessor::LoadTextureImage(filePathW, image);

        // ddsPathToGenerate はBuildPlan内でWICファイルのみ設定される。
        // DDS/HDRの場合は空文字列になるため isDDS/isHDR の重複チェックは不要。
        if (ddsGenerationEnabled && !ddsPath.empty() && SUCCEEDED(hr)) {
            ddsCacheGenerator(resolvedPath, ddsPath);
        }

        if (FAILED(hr)) {
            std::string errorMsg = std::format(
                "Failed to load texture file: {}\nHRESULT: 0x{:08X}\nPlease check if the file exists and the path is correct.",
                resolvedPath,
                static_cast<unsigned int>(hr));
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", errorMsg);
            throw std::runtime_error(errorMsg);
        }

        // シェーダーサンプリング向けにミップチェーンを構築する。
        DirectX::ScratchImage mipImages;
        hr = TextureImageProcessor::BuildMipChain(image, mipImages);

        if (FAILED(hr)) {
            std::string errorMsg = std::format(
                "Failed to generate mipmaps for texture: {}\nHRESULT: 0x{:08X}",
                resolvedPath,
                static_cast<unsigned int>(hr));
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", errorMsg);
            throw std::runtime_error(errorMsg);
        }

        // ミップ作成済み画像をGPUへ転送し、SRVまで作成する。
        ExecutionResult result{};
        result.metadata = mipImages.GetMetadata();
        result.uploadResult = TextureGpuUploader::UploadAndCreateSrv(dxCommon, mipImages, resolvedPath);
        return result;
    }
}


