#pragma once

#include <cstddef>
#include <cstdint>

namespace GameComponents
{
    /// @brief シーンをまたいでゲーム結果を受け渡すための共有データ。
    /// @details GameScene 開始時にリセットし、終了時に値を確定する。
    class GameResultData final
    {
    public:
        static void Reset()
        {
            horizontalProgressBlocks_ = 0;
            monkeyCount_ = 0;
            brokenRockCount_ = 0;
            bananaHarvestCount_ = 0;
        }

        static void SetHorizontalProgressBlocks(uint32_t blocks)
        {
            horizontalProgressBlocks_ = blocks;
        }

        /// @brief 列車が開始位置からX正方向へ進んだ最大ブロック数を取得する。
        /// @note ResultScene からこの getter を呼ぶと、直前のプレイ結果を取得できる。
        static uint32_t GetHorizontalProgressBlocks()
        {
            return horizontalProgressBlocks_;
        }

        /// @brief X正方向の最大進行距離をメートルで取得する。
        /// @details ゲームルール上、1マスを1mとして換算する。
        static uint32_t GetHorizontalProgressMeters()
        {
            constexpr uint32_t kMetersPerBlock = 1;
            return horizontalProgressBlocks_ * kMetersPerBlock;
        }

        static void SetMonkeyCount(std::size_t count)
        {
            monkeyCount_ = count > 0 ? count : 1;
        }

        /// @brief 直前のゲームシーン終了時点のサル数を取得する。
        static std::size_t GetMonkeyCount()
        {
            return monkeyCount_;
        }

        static void AddBrokenRockCount(std::size_t count = 1)
        {
            brokenRockCount_ += count;
        }

        /// @brief 直前のゲームシーンで破壊した岩の数を取得する。
        static std::size_t GetBrokenRockCount()
        {
            return brokenRockCount_;
        }

        static void AddBananaHarvestCount(std::size_t count = 1)
        {
            bananaHarvestCount_ += count;
        }

        /// @brief 直前のゲームシーンで回収したバナナの数を取得する。
        static std::size_t GetBananaHarvestCount()
        {
            return bananaHarvestCount_;
        }

    private:
        static inline uint32_t horizontalProgressBlocks_ = 0;
        static inline std::size_t monkeyCount_ = 0;
        static inline std::size_t brokenRockCount_ = 0;
        static inline std::size_t bananaHarvestCount_ = 0;
    };
}
