#pragma once

#include "Graphics/Cloud/Render/CloudRenderContext.h"
#include "Graphics/RHI/Debug/GpuStageScope.h"

namespace CoreEngine
{
    /// @brief 雲のディスパッチ 1 回分を CloudDetail カテゴリで計測するスコープ
    /// @note 名前はフレーム内で一意にすること（重複させると最後の 1 回だけが残る）。
    class CloudStageScope
    {
    public:
        CloudStageScope(const CloudRenderContext& ctx, const char* name)
            : scope_(ctx.cmdList, ctx.profiler, name, GpuTimingCategory::CloudDetail)
        {
        }

    private:
        GpuStageScope scope_;
    };
}
