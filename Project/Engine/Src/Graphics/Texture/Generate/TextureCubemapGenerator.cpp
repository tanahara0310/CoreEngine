#include "pch.h"
#include "TextureCubemapGenerator.h"
#include "Utility/Logger/Logger.h"
#include "externals/DirectXTex/DirectXTex.h"

#include <format>
#include <filesystem>
#include <cmath>
#include <numbers>

namespace CoreEngine
{
    // ===== Equirectangular → Cubemap 変換用ヘルパー =====

    static constexpr float kPI = std::numbers::pi_v<float>;
    static constexpr float kTwoPI = kPI * 2.0f;

    static void DirToEquirectUV(float dx, float dy, float dz, float& u, float& v)
    {
        float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        dx /= len; dy /= len; dz /= len;
        float phi = std::atan2(dz, dx);
        float theta = std::asin(dy);
        u = (phi + kPI) / kTwoPI;
        v = 1.0f - (theta + kPI * 0.5f) / kPI;
    }

    static void FaceUVToDir(uint32_t face, float u, float v, float& dx, float& dy, float& dz)
    {
        float cu = u * 2.0f - 1.0f;
        float cv = v * 2.0f - 1.0f;
        switch (face)
        {
        case 0: dx = 1.0f; dy = -cv;   dz = -cu;   break; // +X
        case 1: dx = -1.0f; dy = -cv;   dz = cu;   break; // -X
        case 2: dx = cu;   dy = 1.0f;  dz = cv;   break; // +Y
        case 3: dx = cu;   dy = -1.0f;  dz = -cv;   break; // -Y
        case 4: dx = cu;   dy = -cv;   dz = 1.0f;  break; // +Z
        case 5: dx = -cu;   dy = -cv;   dz = -1.0f;  break; // -Z
        default: dx = 0.0f; dy = 1.0f;  dz = 0.0f;  break;
        }
    }

    static void SampleBilinear(
        const DirectX::Image* srcImg,
        uint32_t srcW, uint32_t srcH,
        float u, float v,
        float& r, float& g, float& b)
    {
        u = u - std::floor(u);
        v = (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v;

        float px = u * static_cast<float>(srcW - 1);
        float py = v * static_cast<float>(srcH - 1);
        int x0 = static_cast<int>(px);
        int y0 = static_cast<int>(py);
        int x1 = (x0 + 1 < static_cast<int>(srcW)) ? x0 + 1 : x0;
        int y1 = (y0 + 1 < static_cast<int>(srcH)) ? y0 + 1 : y0;
        float fx = px - static_cast<float>(x0);
        float fy = py - static_cast<float>(y0);

        auto getPixel = [&](int x, int y, int ch) -> float
            {
                const float* row = reinterpret_cast<const float*>(
                    srcImg->pixels + static_cast<size_t>(y) * srcImg->rowPitch);
                return row[x * 4 + ch];
            };

        for (int ch = 0; ch < 3; ch++)
        {
            float val =
                getPixel(x0, y0, ch) * (1.0f - fx) * (1.0f - fy) +
                getPixel(x1, y0, ch) * fx * (1.0f - fy) +
                getPixel(x0, y1, ch) * (1.0f - fx) * fy +
                getPixel(x1, y1, ch) * fx * fy;
            if (ch == 0) r = val;
            else if (ch == 1) g = val;
            else b = val;
        }
    }

    // ===== 公開メソッド =====

    bool TextureCubemapGenerator::GenerateFromHDR(const std::string& hdrPath, const std::string& cubemapDDSPath) const
    {
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}",
            std::format("Equirectangular to cubemap conversion: {} -> {}", hdrPath, cubemapDDSPath));

        // HDR ファイルを読み込む
        std::wstring hdrPathW = Logger::GetInstance().ConvertString(hdrPath);
        DirectX::ScratchImage hdrImage;
        HRESULT hr = DirectX::LoadFromHDRFile(hdrPathW.c_str(), nullptr, hdrImage);
        if (FAILED(hr))
        {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}",
                std::format("Failed to load HDR file: {} (HRESULT=0x{:08X})", hdrPath, static_cast<unsigned int>(hr)));
            return false;
        }

        const DirectX::Image* srcImg = hdrImage.GetImage(0, 0, 0);
        if (!srcImg || !srcImg->pixels)
        {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "HDR image has no pixel data");
            return false;
        }

        // RGBA32F に変換（DirectX::LoadFromHDRFile は通常 RGBA32F だが念のため）
        DirectX::ScratchImage convertedHdr;
        if (srcImg->format != DXGI_FORMAT_R32G32B32A32_FLOAT)
        {
            hr = DirectX::Convert(*srcImg, DXGI_FORMAT_R32G32B32A32_FLOAT,
                DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, convertedHdr);
            if (FAILED(hr))
            {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Failed to convert HDR to RGBA32F");
                return false;
            }
            srcImg = convertedHdr.GetImage(0, 0, 0);
        }

        const uint32_t srcW = static_cast<uint32_t>(srcImg->width);
        const uint32_t srcH = static_cast<uint32_t>(srcImg->height);
        const uint32_t faceSize = outputFaceSize_;

        // 出力キューブマップ（RGBA32F × 6面）
        DirectX::ScratchImage cubemap;
        hr = cubemap.InitializeCube(DXGI_FORMAT_R32G32B32A32_FLOAT, faceSize, faceSize, 1, 1);
        if (FAILED(hr))
        {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Failed to initialize cubemap ScratchImage");
            return false;
        }

        // 全6面をサンプリング
        for (uint32_t face = 0; face < 6; face++)
        {
            const DirectX::Image* dstImg = cubemap.GetImage(0, face, 0);
            if (!dstImg) continue;

            for (uint32_t y = 0; y < faceSize; y++)
            {
                float* row = reinterpret_cast<float*>(
                    dstImg->pixels + static_cast<size_t>(y) * dstImg->rowPitch);

                for (uint32_t x = 0; x < faceSize; x++)
                {
                    float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(faceSize);
                    float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(faceSize);

                    float dx, dy, dz;
                    FaceUVToDir(face, u, v, dx, dy, dz);

                    float eu, ev;
                    DirToEquirectUV(dx, dy, dz, eu, ev);

                    float r = 0.0f, g = 0.0f, b = 0.0f;
                    SampleBilinear(srcImg, srcW, srcH, eu, ev, r, g, b);

                    row[x * 4 + 0] = r;
                    row[x * 4 + 1] = g;
                    row[x * 4 + 2] = b;
                    row[x * 4 + 3] = 1.0f;
                }
            }
        }

        // 出力ディレクトリを作成して DDS 保存
        std::filesystem::path outPath(cubemapDDSPath);
        std::filesystem::create_directories(outPath.parent_path());

        std::wstring outPathW = Logger::GetInstance().ConvertString(cubemapDDSPath);
        hr = DirectX::SaveToDDSFile(
            cubemap.GetImages(), cubemap.GetImageCount(),
            cubemap.GetMetadata(), DirectX::DDS_FLAGS_NONE, outPathW.c_str());

        if (FAILED(hr))
        {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}",
                std::format("Failed to save cubemap DDS: {} (HRESULT=0x{:08X})", cubemapDDSPath, static_cast<unsigned int>(hr)));
            return false;
        }

        return ValidateGeneratedCubemap(cubemapDDSPath);
    }

    bool TextureCubemapGenerator::ValidateGeneratedCubemap(const std::string& filePath) const
    {
        if (!std::filesystem::exists(filePath))
            return false;

        try
        {
            auto fileSize = std::filesystem::file_size(filePath);
            if (fileSize == 0)
            {
                Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Graphics, "{}",
                    std::format("Cubemap file is empty: {}", filePath));
                return false;
            }

            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}",
                std::format("Cubemap DDS generated: {} (size: {} bytes)", filePath, fileSize));
            return true;
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Graphics, "{}",
                std::format("Failed to validate cubemap file: {}", e.what()));
            return false;
        }
    }
}


