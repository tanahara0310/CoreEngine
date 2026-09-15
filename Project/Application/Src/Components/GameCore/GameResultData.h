#pragma once

#include "Utility/Session/SessionValues.h"

#include <cstddef>
#include <cstdint>

namespace GameComponents
{
    /// @brief シーンをまたいでゲーム結果を受け渡すための共有データ。
    /// @details 値はエンジンの SessionValues に下の名前で置く（スクリプトからは Session で同じ名前を読む）。
    ///          GameScene 開始時にリセットし、終了時に値を確定する。
    class GameResultData final
    {
    public:
        static constexpr const char* kHorizontalProgressBlocksKey = "GameResult.HorizontalProgressBlocks";
        static constexpr const char* kMonkeyCountKey = "GameResult.MonkeyCount";
        static constexpr const char* kBrokenRockCountKey = "GameResult.BrokenRockCount";
        static constexpr const char* kBananaHarvestCountKey = "GameResult.BananaHarvestCount";

        static void Reset()
        {
            CoreEngine::SessionValues::SetInt(kHorizontalProgressBlocksKey, 0);
            CoreEngine::SessionValues::SetInt(kMonkeyCountKey, 0);
            CoreEngine::SessionValues::SetInt(kBrokenRockCountKey, 0);
            CoreEngine::SessionValues::SetInt(kBananaHarvestCountKey, 0);
        }

        static void SetHorizontalProgressBlocks(uint32_t blocks)
        {
            CoreEngine::SessionValues::SetInt(kHorizontalProgressBlocksKey, blocks);
        }

        /// @brief 列車が開始位置からX正方向へ進んだ最大ブロック数を取得する。
        /// @note ResultScene からこの getter を呼ぶと、直前のプレイ結果を取得できる。
        static uint32_t GetHorizontalProgressBlocks()
        {
            return static_cast<uint32_t>(CoreEngine::SessionValues::GetInt(kHorizontalProgressBlocksKey, 0));
        }

        /// @brief X正方向の最大進行距離をメートルで取得する。
        /// @details ゲームルール上、1マスを1mとして換算する。
        static uint32_t GetHorizontalProgressMeters()
        {
            constexpr uint32_t kMetersPerBlock = 1;
            return GetHorizontalProgressBlocks() * kMetersPerBlock;
        }

        static void SetMonkeyCount(std::size_t count)
        {
            CoreEngine::SessionValues::SetInt(kMonkeyCountKey, static_cast<std::int64_t>(count > 0 ? count : 1));
        }

        /// @brief 直前のゲームシーン終了時点のサル数を取得する。
        static std::size_t GetMonkeyCount()
        {
            return static_cast<std::size_t>(CoreEngine::SessionValues::GetInt(kMonkeyCountKey, 0));
        }

        static void AddBrokenRockCount(std::size_t count = 1)
        {
            CoreEngine::SessionValues::SetInt(kBrokenRockCountKey,
                CoreEngine::SessionValues::GetInt(kBrokenRockCountKey, 0) + static_cast<std::int64_t>(count));
        }

        /// @brief 直前のゲームシーンで破壊した岩の数を取得する。
        static std::size_t GetBrokenRockCount()
        {
            return static_cast<std::size_t>(CoreEngine::SessionValues::GetInt(kBrokenRockCountKey, 0));
        }

        static void AddBananaHarvestCount(std::size_t count = 1)
        {
            CoreEngine::SessionValues::SetInt(kBananaHarvestCountKey,
                CoreEngine::SessionValues::GetInt(kBananaHarvestCountKey, 0) + static_cast<std::int64_t>(count));
        }

        /// @brief 直前のゲームシーンで回収したバナナの数を取得する。
        static std::size_t GetBananaHarvestCount()
        {
            return static_cast<std::size_t>(CoreEngine::SessionValues::GetInt(kBananaHarvestCountKey, 0));
        }
    };
}
