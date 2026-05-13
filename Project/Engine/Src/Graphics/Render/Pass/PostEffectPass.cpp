#include "PostEffectPass.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"

namespace CoreEngine
{
    void PostEffectPass::Execute(const RenderContext& context)
    {
        if (!context.postEffectManager) {
            return;
        }

        // 入力ハンドルが設定されていない場合はデフォルトを使用
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrv = inputHandle_;
        if (inputSrv.ptr == 0 && context.dxCommon) {
#ifdef _DEBUG
            OutputDebugStringA("[PostEffectPass] WARNING: inputHandle_.ptr == 0, falling back to GetOffScreenSrvHandle(0). "
                               "Check if GeometryPass output is correctly forwarded.\n");
#endif
            inputSrv = context.dxCommon->GetOffScreenSrvHandle();
        }

        // ポストエフェクトチェーンの適用
        D3D12_GPU_DESCRIPTOR_HANDLE result = 
            context.postEffectManager->ExecuteEffectChain(inputSrv);

        // 出力を設定（次のパスに渡す）
        output_.srvHandle = result;
        output_.isValid = true;
    }
}
