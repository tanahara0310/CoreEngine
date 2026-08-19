#include "pch.h"
#include "TextureMetadataLoader.h"
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

        HRESULT hr = TextureImageProcessor::LoadMetadata(resolvedPath.wstring(), metadata);
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
