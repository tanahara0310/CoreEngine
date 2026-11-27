#include "Capture/SourceInfoCache.h"

#include "Capture/TracyAccess.h"

namespace CoreEngine::Profiler
{
    SourceInfoCache::SourceInfoCache()
        : entries_(65536)
    {
    }

    void SourceInfoCache::Reset()
    {
        std::fill(entries_.begin(), entries_.end(), SourceInfo{});
    }

    const SourceInfo& SourceInfoCache::Get(const tracy::Worker& worker, int16_t srcloc)
    {
        SourceInfo& entry = entries_[static_cast<uint16_t>(srcloc)];
        if (!entry.resolved) {
            const tracy::SourceLocation& location = worker.GetSourceLocation(srcloc);
            entry.name = Access::SourceName(worker, srcloc);
            entry.file = worker.GetString(location.file);
            entry.line = location.line;
            entry.kind = ClassifyZone(entry.name, entry.file);
            entry.resolved = Access::SourceArrived(worker, srcloc);
        }
        return entry;
    }
}
