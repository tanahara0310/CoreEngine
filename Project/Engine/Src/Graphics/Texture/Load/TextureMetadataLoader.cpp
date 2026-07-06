#include "pch.h"
#include "TextureMetadataLoader.h"
#include "Graphics/Texture/Load/TextureImageProcessor.h"
#include "Utility/Logger/Logger.h"

#include <format>
#include <stdexcept>

namespace CoreEngine
{
    DirectX::TexMetadata TextureMetadataLoader::LoadOrThrow(const std::string& resolvedPath)
    {
        // 解決済みパスをワイド文字列へ変換し、DirectXTexのメタデータ取得APIへ渡す。
        std::wstring filePathW = Logger::GetInstance().ConvertString(resolvedPath);
        DirectX::TexMetadata metadata{};

        HRESULT hr = TextureImageProcessor::LoadMetadata(filePathW, metadata);
        if (FAILED(hr)) {
            // 失敗時はログと例外で上位へ通知し、呼び出し元でフォールバックを判断する。
            std::string errorMsg = std::format(
                "Failed to load texture file: {}\nHRESULT: 0x{:08X}\nPlease check if the file exists and the path is correct.",
                resolvedPath,
                static_cast<unsigned int>(hr));
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", errorMsg);
            throw std::runtime_error(errorMsg);
        }

        return metadata;
    }
}


