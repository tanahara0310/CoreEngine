#include "pch.h"
#include "WaterShoreMeshDisplacementPass.h"

#include "Graphics/Water/Surface/WaterShoreMeshDisplacement.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    void WaterShoreMeshDisplacementPass::Execute(const RenderContext& context)
    {
        WaterShoreMeshDisplacement* const displacement = context.waterShoreMeshDisplacement;
        if (!displacement || !displacement->IsInitialized() || !context.cmdList || !context.waterSurfaceState) {
            return;
        }
        // 範囲はゲームのカメラの周りでビューに依らないので、フレームに 1 回だけ書く
        displacement->Dispatch(context.cmdList, *context.waterSurfaceState, context.frameNumber);
    }
}
