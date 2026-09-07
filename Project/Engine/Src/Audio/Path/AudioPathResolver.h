#pragma once

#include <filesystem>
#include <string>

namespace CoreEngine
{
    /// @brief 音声ファイルのパス解決を担当するクラス
    /// @details パスは一貫して std::filesystem::path で扱う。narrow 文字列を経由すると
    ///          ANSI / UTF-8 のどちらなのかが型から失われ、非 ASCII を含むパスで
    ///          ファイルを開けなくなる（TexturePathResolver と同じ方針）。
    class AudioPathResolver {
    public:
        /// @brief ファイルパスを解決する。Assets フォルダを省略したパスも受け付ける
        /// @param filePath 解決したいファイルパス（UTF-8）
        /// @return 解決されたファイルパス
        static std::filesystem::path Resolve(const std::string& filePath);

        /// @brief 小文字に正規化した拡張子を返す（例: ".wav"）。無ければ空文字列
        static std::string GetLowerExtension(const std::filesystem::path& path);

    private:
        /// @brief Assets フォルダを省略されたときに前置するパス
        static constexpr const char* kDefaultBasePath = "Application/Assets/";
    };
}
