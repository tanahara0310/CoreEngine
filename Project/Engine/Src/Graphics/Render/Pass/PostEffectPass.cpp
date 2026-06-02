#include "pch.h"
#include "PostEffectPass.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/Outline/Outline.h"
#include "Camera/CameraManager.h"
#include "Camera/ICamera.h"
#include "Camera/CameraStructs.h"

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
        // Outlineエフェクトが有効な場合、カメラのnear/farを毎フレーム同期する
        if (context.cameraManager) {
            if (auto* outline = context.postEffectManager->GetEffect<Outline>(PostEffectNames::Outline)) {
                if (outline->IsEnabled()) {
                    if (const ICamera* cam = context.cameraManager->GetActiveCamera(CameraType::Camera3D)) {
                        const CameraParameters params = cam->GetParameters();
                        outline->SetCameraClipPlanes(params.nearClip, params.farClip);
                    }
                }
            }
        }

        D3D12_GPU_DESCRIPTOR_HANDLE result = 
            context.postEffectManager->ExecuteEffectChain(inputSrv);

        // 出力を設定（次のパスに渡す）
        output_.srvHandle = result;
        output_.isValid = true;
    }
}
