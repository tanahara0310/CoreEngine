#pragma once

#include "Math/Vector/Vector4.h"
#include "Utility/CVar/CVar.h"

namespace CoreEngine
{
    /// @brief ボリュメトリックフォグの CVar 群（定義は VolumetricFogCVars.cpp）
    namespace VolumetricFogCVars
    {
        extern CVar<bool>    Enabled;
        extern CVar<float>   Distance;
        extern CVar<int>     GridPixelSize;
        extern CVar<int>     GridSizeZ;
        extern CVar<float>   DepthDistribution;
        extern CVar<Vector4> Albedo;
        extern CVar<float>   Anisotropy;
        extern CVar<float>   AmbientScale;
        extern CVar<float>   HistoryWeight;
        extern CVar<int>     DebugView;
    }
}
