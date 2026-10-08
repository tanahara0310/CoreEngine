#include "pch.h"
#include "TextureMetadataLoader.h"
#include "Graphics/Texture/Cook/CookedTexture.h"
#include "Graphics/Texture/Load/TextureImageProcessor.h"
#include "Utility/Logger/Logger.h"

#include <filesystem>
#include <format>
#include <stdexcept>

namespace CoreEngine
{
    DirectX::TexMetadata TextureMetadataLoader::LoadOrThrow(const std::filesystem::path& resolvedPath)
    {
        // DirectXTex はワイド文字列の API なので、ここで初めてワイドへ変換する。
        // path のまま運んできたのでエンコーディングの取り違えは起こらない。
        DirectX::TexMetadata metadata{};

        // 書き出したゲームでは、元の画像の代わりにクック済みの DDS から読む
        std::filesystem::path readPath = CookedTexture::Find(resolvedPath, TextureColorSpace::SRGB);
        if (readPath.empty()) {
            readPath = CookedTexture::Find(resolvedPath, TextureColorSpace::Linear);
        }
        if (readPath.empty()) {
            readPath = resolvedPath;
        }

        HRESULT hr = TextureImageProcessor::LoadMetadata(readPath.wstring(), metadata);
        if (FAILED(hr)) {
            // 失敗時はログと例外で上位へ通知し、呼び出し元でフォールバックを判断する。
            std::string errorMsg = std::format(
                "Failed to load texture file: {}\nHRESULT: 0x{:08X}\nPlease check if the file exists and the path is correct.",
                Logger::GetInstance().PathToUtf8(resolvedPath),
                static_cast<unsigned int>(hr));
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", errorMsg);
            throw std::runtime_error(errorMsg);
        }

        return metadata;
    }
}
