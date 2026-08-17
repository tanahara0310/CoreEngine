#pragma once

#include <string>
#include <filesystem>

namespace CoreEngine
{
    /// @brief HDRからキューブマップDDSを生成する責務を持つクラス
    /// @details Equirectangular HDR をCPU処理でキューブマップに変換し、DDSとして保存する
    class TextureCubemapGenerator final {
    public:
        /// @brief HDR画像からキューブマップDDSを生成する
        /// @param hdrPath 入力HDRパス
        /// @param cubemapDDSPath 出力DDSパス
        /// @return 生成と検証に成功したらtrue
        bool GenerateFromHDR(const std::filesystem::path& hdrPath, const std::filesystem::path& cubemapDDSPath) const;

        /// @brief キューブマップ出力フェイスサイズを設定する（デフォルト512px）
        /// @param faceSize 各面のピクセル数（512推奨。小さいほど高速、大きいほど高品質）
        void SetOutputFaceSize(uint32_t faceSize) { outputFaceSize_ = faceSize; }

    private:
        /// @brief 生成されたキューブマップDDSを存在・サイズで検証する
        /// @param filePath 検証対象ファイル
        /// @return 有効ファイルならtrue
        bool ValidateGeneratedCubemap(const std::filesystem::path& filePath) const;

        uint32_t outputFaceSize_ = 512;  ///< キューブマップ出力フェイスサイズ（px）
    };
}
