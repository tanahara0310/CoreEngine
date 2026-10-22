#pragma once

#include <cstdint>

namespace CoreEngine
{
    /// @brief プロセス全体で使えるワーカースレッド数の総量管理
    /// @details 独立したスレッドプールが 3 つあるので、各自が hardware_concurrency / 2 を
    ///          取ると物理コアを超えるスレッドが同時に走り、レイテンシだけが悪化する。
    /// @note 上限はコア数 - 1。メインスレッド（メッセージと描画）のぶんを必ず 1 本残す。
    namespace ThreadBudget
    {
        /// @brief ずっと残るプールの取り分（枠に対する比率）
        inline constexpr float kModelLoadShare = 0.4f;
        inline constexpr float kTextureLoadShare = 0.4f;
        inline constexpr float kFontBakeShare = 0.2f;

        /// @brief 割り当て可能なワーカー総数（コア数 - 1、最低 1）
        uint32_t GetCapacity();

        /// @brief 枠に比率を掛けたワーカー数（最低 1。上限があればそこまで）
        uint32_t Share(float ratio);

        /// @brief 1 つのプールのワーカー数の上限を決める（0 = 上限なし）
        /// @note どのプールも作る前に 1 回だけ呼ぶ。1 にすると全プールが 1 本になる（順番に頼る不具合を探すのに使う）
        void SetWorkerLimit(uint32_t limit);

        /// @brief 1 つのプールのワーカー数の上限（0 = 上限なし）
        uint32_t GetWorkerLimit();

        /// @brief 現在予約されている総ワーカー数
        uint32_t GetReserved();

        /// @brief ワーカー枠を予約する
        /// @param requested 希望数。0 なら「残り枠の半分」を要求する
        /// @return 実際に割り当てられた数（最低 1）
        /// @note 最低 1 を返すのは、ワーカー 0 のプールを作らせないため（Submit が永久にブロックする）
        uint32_t Reserve(uint32_t requested);

        /// @brief 予約を返却する（ThreadPool のデストラクタから呼ばれる）
        void Release(uint32_t count);
    }
}
