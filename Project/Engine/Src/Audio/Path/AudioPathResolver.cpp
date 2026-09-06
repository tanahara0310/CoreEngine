#include "pch.h"
#include "AudioPathResolver.h"

#include <algorithm>
#include <cctype>

#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    std::filesystem::path AudioPathResolver::Resolve(const std::string& filePath)
    {
        // 入力パスのバックスラッシュをスラッシュに統一してから前置判定する
        std::string normalized = filePath;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');

        const bool alreadyRooted =
            normalized.starts_with("Application/Assets/") ||
            normalized.starts_with("Engine/Assets/") ||
            // 絶対パス（"C:/..." など）
            (normalized.length() >= 2 && normalized[1] == ':');

        if (!alreadyRooted) {
            normalized.insert(0, kDefaultBasePath);
        }

        // UTF-8 → path。ここを narrow のまま渡すと ANSI 解釈されて非 ASCII が開けない
        return Logger::GetInstance().Utf8ToPath(normalized);
    }

    std::string AudioPathResolver::GetLowerExtension(const std::filesystem::path& path)
    {
        // 拡張子は ASCII 前提なので narrow に落として良い
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension;
    }
}
