#include "pch.h"
#include "Graphics/RHI/Debug/GpuMarker.h"

#ifdef USE_PIX
#include <d3d12.h>
#include <pix3.h>
#endif

namespace CoreEngine
{
    void BeginGpuMarker([[maybe_unused]] ID3D12GraphicsCommandList* cmdList, [[maybe_unused]] const char* name)
    {
#ifdef USE_PIX
        if (!cmdList || !name) {
            return;
        }
        // 色は PIX 上のイベント帯の色。単色で十分なため既定色を使う。
        PIXBeginEvent(cmdList, PIX_COLOR_DEFAULT, "%s", name);
#endif
    }

    void EndGpuMarker([[maybe_unused]] ID3D12GraphicsCommandList* cmdList)
    {
#ifdef USE_PIX
        if (!cmdList) {
            return;
        }
        PIXEndEvent(cmdList);
#endif
    }
}
