#pragma once

#include <cstdint>

namespace GameComponents
{
    /// @brief 走った距離の記録を JSON 1 ファイルへ残し、次のリザルトで比べられるようにする。
    ///
    /// @details リザルトで「今回はどうだったのか」を出すには、比べる相手が要る。
    ///          目標（500m）はゲーム側が決めた共通の物差しだが、それだけだと
    ///          目標に遠い人には何も返らない。そこで **前回の自分** を杭として立てる。
    ///
    /// @note 保存先は作業ディレクトリ基準の `Application/Config/result_record.json`。
    ///       CVars.json と同じ場所なので、書けない環境なら CVar の保存も同じく失敗している。
    ///       読み書きに失敗しても「記録なし」として静かに続行する（遊べなくなるより良い）。
    class GameRecordStore final
    {
    public:
        struct Record
        {
            std::uint32_t previous = 0;   ///< 前回の走行距離 [m]
            std::uint32_t best = 0;       ///< これまでの最高距離 [m]
            bool hasPrevious = false;     ///< 前回の記録があるか（初回は false）
            bool isNewBest = false;       ///< 今回が自己最高を更新したか
        };

        /// @brief 今回の距離を記録し、**更新前** の前回・最高を返す。
        /// @param meters 今回の走行距離 [m]
        /// @return 比較用の記録。`previous` は今回の 1 つ前、`best` は今回を含めない最高
        /// @note リザルトに入るたびに 1 回だけ呼ぶこと。
        static Record CommitRun(std::uint32_t meters);

        /// @brief 記録を書き換えずに読むだけ（デバッグ表示用）
        static Record Peek();
    };
}
