#include "pch.h"
#include "TextureLoadExecutor.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Texture/Load/TextureImageProcessor.h"
#include "Graphics/Texture/Gpu/TextureGpuUploader.h"
#include "Utility/Logger/Logger.h"

#include <filesystem>
#include <format>
#include <stdexcept>

namespace CoreEngine
{
    TextureLoadExecutor::ExecutionResult TextureLoadExecutor::Execute(
        CoreEngine::GraphicsCore* dxCommon,
        const std::filesystem::path& resolvedPath,
        bool ddsGenerationEnabled,
        const std::filesystem::path& ddsPath,
        const std::function<bool(const std::filesystem::path&, const std::filesystem::path&)>& ddsCacheGenerator,
        TextureColorSpace colorSpace)
    {
        Logger& log = Logger::GetInstance();

        // DirectXTex はワイド文字列の API なので、ここで初めてワイドへ変換する。
        // ログ・SRV 名として使う表示用文字列は UTF-8 に変換した別物として扱う。
        const std::string pathForDisplay = log.PathToUtf8(resolvedPath);

        // 画像データを読み込み、必要であれば後続でDDS生成も実行する。
        DirectX::ScratchImage image;
        HRESULT hr = TextureImageProcessor::LoadTextureImage(resolvedPath.wstring(), image, colorSpace);

        // ddsPathToGenerate が非空 ＝「まだキャッシュが無い WIC ファイル」だけ。
        // DDS/HDR 入力と、キャッシュヒットした場合は BuildPlan 側で空にしてあるので、
        // ここで isDDS/isHDR やヒット有無を再判定する必要はない。
        if (ddsGenerationEnabled && !ddsPath.empty() && SUCCEEDED(hr)) {
            ddsCacheGenerator(resolvedPath, ddsPath);
        }

        if (FAILED(hr)) {
            std::string errorMsg = std::format(
                "Failed to load texture file: {}\nHRESULT: 0x{:08X}\nPlease check if the file exists and the path is correct.",
                pathForDisplay,
                static_cast<unsigned int>(hr));
            log.Logf(LogLevel::Error, LogCategory::Graphics, "{}", errorMsg);
            throw std::runtime_error(errorMsg);
        }

        // シェーダーサンプリング向けにミップチェーンを構築する。
        DirectX::ScratchImage mipImages;
        hr = TextureImageProcessor::BuildMipChain(image, mipImages);

        if (FAILED(hr)) {
            std::string errorMsg = std::format(
                "Failed to generate mipmaps for texture: {}\nHRESULT: 0x{:08X}",
                pathForDisplay,
                static_cast<unsigned int>(hr));
            log.Logf(LogLevel::Error, LogCategory::Graphics, "{}", errorMsg);
            throw std::runtime_error(errorMsg);
        }

        // ミップ作成済み画像をGPUへ転送し、SRVまで作成する。
        ExecutionResult result{};
        result.metadata = mipImages.GetMetadata();
        result.uploadResult = TextureGpuUploader::UploadAndCreateSrv(dxCommon, mipImages, pathForDisplay);
        return result;
    }
}
