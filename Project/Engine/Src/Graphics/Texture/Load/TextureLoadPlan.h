#pragma once

#include "Graphics/Texture/Path/TexturePathResolver.h"

#include <string>
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
            std::string resolvedPath;      // 実際に読み込む最終パス
            bool isDDS = false;            // DDSとして読み込むか
            bool isHDR = false;            // HDRとして読み込むか
            std::string ddsPathToGenerate; // WIC読み込み後に生成するDDSパス
        };

        /// @brief ファイル形式とキャッシュ状態から読み込み計画を構築する
        /// @param resolvedPath 入力時に解決済みのパス
        /// @param ddsGenerationEnabled DDS生成の有効/無効
        /// @param pathResolver パス解決ヘルパー
        /// @param cubemapGenerator HDR->Cubemap DDS生成関数
        /// @return 実行に使う読み込み計画
        PlanResult BuildPlan(
            const std::string& resolvedPath,
            bool ddsGenerationEnabled,
            const TexturePathResolver& pathResolver,
            const std::function<bool(const std::string&, const std::string&)>& cubemapGenerator) const;
    };
}
