// 高さフォグの合成。HeightFog.CS.hlsl と同じで、格子より奥まで届く局所の霧を調べない
// （格子より奥まで届く局所の霧が無いフレームに FogManager が使う）

#define VOLUMETRIC_FOG_FAR_VOLUMES 0
#include "HeightFog.CS.hlsl"
