#pragma once

#include "Graphics/Texture/Path/TexturePathResolver.h"
#include "Graphics/Texture/TextureColorSpace.h"

#include <string>
#include <filesystem>
#include <functional>

namespace CoreEngine
{
    /// @brief 読み込み前のパス選択とキャッシュ利用判定をまとめた計画クラス
    class TextureLoadPlan
    {
    public:
        /// @brief 実際にどのファイルを読むかを表す計画結果
        struct PlanResult
        {
            std::filesystem::path resolvedPath;      // 実際に読み込む最終パス
            bool isDDS = false;                      // DDSとして読み込むか
        };

        /// @brief ファイル形式とキャッシュ状態から読み込み計画を構築する
        /// @param colorSpace 色空間（派生データの版を選ぶのに使う）
        /// @throw std::runtime_error 変換する画像の DDS を用意できなかったとき
        PlanResult BuildPlan(
            const std::filesystem::path& resolvedPath,
            bool ddsGenerationEnabled,
            const TexturePathResolver& pathResolver,
            const std::function<bool(const std::filesystem::path&, const std::filesystem::path&)>& cubemapGenerator,
            TextureColorSpace colorSpace = TextureColorSpace::SRGB) const;
    };
}
