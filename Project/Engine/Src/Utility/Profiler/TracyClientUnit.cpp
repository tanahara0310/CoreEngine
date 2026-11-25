// Tracy の本体（TracyClient.cpp）を ProfileZone.h と同じ設定でコンパイルする。
// 外部のコードなのでプリコンパイル済みヘッダを使わず、警告の水準を下げてコンパイルする。
#include "Utility/Profiler/ProfileZone.h"

#if CORE_TRACY_ENABLED
#include "Externals/tracy/public/TracyClient.cpp"
#endif
