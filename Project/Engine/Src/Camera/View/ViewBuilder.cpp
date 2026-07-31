#include "pch.h"
#include "ViewBuilder.h"

#include "Camera/ICamera.h"
#include "Camera/CameraStructs.h"

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    ViewInfo ViewBuilder::Build(const ICamera* camera, RenderViewType type)
    {
        ViewInfo view{};
        view.type = type;

        if (!camera) {
            return view;
        }

        view.camera = camera;
        view.viewMatrix = camera->GetViewMatrix();
        view.projection = camera->GetProjectionMatrix();
        view.viewProjection = Matrix::Multiply(view.viewMatrix, view.projection);
        view.invViewProjection = Matrix::Inverse(view.viewProjection);
        view.position = camera->GetPosition();

        // 視錐台はここで 1 回だけ抽出する。以前はモデル単位のカリングから
        // ICamera::GetFrustum() を呼ぶたびに VP 乗算 + 6 平面抽出をやり直していた。
        view.frustum.ExtractFromMatrix(view.viewProjection);

        const CameraParameters params = camera->GetParameters();
        view.nearZ = params.nearClip;
        view.farZ = params.farClip;

        view.jitterNdc = { camera->GetProjectionJitterX(), camera->GetProjectionJitterY() };
        view.cameraCBV = camera->GetGPUVirtualAddress();
        view.isValid = true;

        return view;
    }
}
