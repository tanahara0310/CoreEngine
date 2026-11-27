#pragma once

#include "Capture/ZoneKind.h"

#include <cstdint>
#include <vector>

namespace tracy
{
    class Worker;
}

/// @file SourceInfoCache.h
/// @brief 区間の場所の番号ごとに、名前・ファイル・行・種類を控える（鍵を放した後の描画で引く）

namespace CoreEngine::Profiler
{
    /// @brief 区間の場所 1 つ分
    struct SourceInfo
    {
        const char* name = "";   ///< 区間の名前（受け取り役の文字。記録を差し替えるまで消えない）
        const char* file = "";   ///< 場所のファイル（フルパス）
        uint32_t line = 0;
        ZoneKind kind = ZoneKind::Other;
        bool filled = false;
    };

    class SourceInfoCache
    {
    public:
        SourceInfoCache();

        /// @brief 控えを全部捨てる（記録を差し替えたとき）
        void Reset();

        /// @brief 鍵をかけた受け取り役から引く。初めての場所なら控える
        const SourceInfo& Get(const tracy::Worker& worker, int16_t srcloc);

        /// @brief 控えた分だけを引く（鍵の外で使う。控えていなければ空）
        const SourceInfo& Peek(int16_t srcloc) const { return entries_[static_cast<uint16_t>(srcloc)]; }

    private:
        std::vector<SourceInfo> entries_;
    };
}
