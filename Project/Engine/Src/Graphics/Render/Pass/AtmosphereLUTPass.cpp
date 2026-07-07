#include "pch.h"
#include "AtmosphereLUTPass.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Common/DirectXCommon.h"

namespace CoreEngine
{
    void AtmosphereLUTPass::Execute(const RenderContext& context)
    {
        if (!context.dxCommon || !context.atmosphereManager) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.dxCommon->GetCommandList();
        if (!cmdList) {
            return;
        }

        // ダーティフラグが立っている場合のみ内部で再計算される
        context.atmosphereManager->GenerateLUTsIfNeeded(cmdList);
    }
}
