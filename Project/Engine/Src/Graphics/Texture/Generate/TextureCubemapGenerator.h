#pragma once

#include <string>
#include <filesystem>

namespace CoreEngine
{
    /// @brief HDRからキューブマップDDSを生成する責務を持つクラス
    class TextureCubemapGenerator
    {
    public:
        /// @brief HDR画像からキューブマップDDSを生成する
        /// @param hdrPath 入力HDRパス
        /// @param cubemapDDSPath 出力DDSパス
        /// @return 生成と検証に成功したらtrue
        bool GenerateFromHDR(const std::string& hdrPath, const std::string& cubemapDDSPath) const;

    private:
        /// @brief cmft実行ファイルの存在を確認してパスを返す
        /// @return cmft実行ファイルパス。未検出なら空パス
        std::filesystem::path FindCmftExecutable() const;

        /// @brief Windowsパスをcmft向けのUnix形式パスへ変換する
        /// @param path 変換元パス
        /// @return スラッシュ区切りの文字列パス
        static std::string ConvertToUnixPath(const std::filesystem::path& path);

        /// @brief 生成されたキューブマップDDSを存在・サイズで検証する
        /// @param filePath 検証対象ファイル
        /// @return 有効ファイルならtrue
        bool ValidateGeneratedCubemap(const std::string& filePath) const;

        static constexpr int processWaitTimeOutMs_ = 100;  ///< プロセス実行後の待機時間（ms）
        static constexpr const char* cmtfRelativePath_ = "Externals/cmft/cmftRelease.exe";  ///< cmft実行ファイル相対パス
    };
}
