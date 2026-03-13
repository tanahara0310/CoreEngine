#pragma once

#include <string>

namespace CoreEngine
{
    class TexturePathResolver {
    public:

        /// @brief ファイルパスを解決する。Assetsフォルダを省略したパスも受け付ける。
        /// @param filePath 解決したいファイルパス（Assetsフォルダを省略可能）
        /// @param writeLog 解決結果をログに書き出すかどうか（デバッグ用、デフォルトはfalse）
        /// @return 解決されたファイルパス。Assetsフォルダを省略した場合は自動的に追加される。
        std::string ResolveAssetPath(const std::string& filePath, bool writeLog = false) const;

        /// @brief DDSキャッシュのパスを取得する。元のファイルパスから対応するDDSファイルのパスを生成する。
        /// @param originalPath 元のファイルパス（Assetsフォルダを省略可能）
        /// @return DDSファイルのパス。Assetsフォルダを省略した場合は自動的に追加される。
        std::string GetDDSCachePath(const std::string& originalPath) const;

        /// @brief HDRファイルから生成されるキューブマップDDSのパスを取得する。元のファイルパスから対応するキューブマップDDSファイルのパスを生成する。
        /// @param originalPath 元のファイルパス（Assetsフォルダを省略可能）
        /// @return キューブマップDDSファイルのパス。Assetsフォルダを省略した場合は自動的に追加される。
        std::string GetCubemapDDSPath(const std::string& originalPath) const;

    private:

        /// @brief ファイルパスを解決する。Assetsフォルダを省略したパスも受け付ける。
        /// @param filePath 解決したいファイルパス（Assetsフォルダを省略可能）
        /// @return 解決されたファイルパス。Assetsフォルダを省略した場合は自動的に追加される。
        std::string ResolveFilePath(const std::string& filePath) const;

        const std::string basePath_ = "Application/Assets/";
    };
}
