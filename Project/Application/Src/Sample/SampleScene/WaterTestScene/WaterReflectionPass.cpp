#include "pch.h"
#include "WaterReflectionPass.h"

#include "Camera/ICamera.h"
#include "Camera/Release/Camera.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"

#include <DirectXMath.h>

using namespace CoreEngine;
using namespace DirectX;

void WaterReflectionPass::SetupReflectionCamera(ICamera* mainCamera, float waterHeight)
{
    if (!mainCamera) {
        hasSavedCameraState_ = false;
        return;
    }

    // ReflectionView 実行中に水面より上だけを描画するため、クリップ平面を更新する。
    clipPlane_ = { 0.0f, 1.0f, 0.0f, -waterHeight };
    clipEnabled_ = true;

    Logger::GetInstance().Infof(
        CoreEngine::LogCategory::Graphics,
        CoreEngine::LogSubCategory::RenderTarget,
        "WaterReflectionPass Render: waterHeight={:.3f} cameraPos=({:.3f}, {:.3f}, {:.3f}) clipPlane=({:.3f}, {:.3f}, {:.3f}, {:.3f})",
        waterHeight,
        mainCamera->GetPosition().x,
        mainCamera->GetPosition().y,
        mainCamera->GetPosition().z,
        clipPlane_.x,
        clipPlane_.y,
        clipPlane_.z,
        clipPlane_.w);

    Matrix4x4 reflectedView = CalcReflectedViewMatrix(mainCamera, waterHeight);

    // 反射実行後に元へ戻せるよう、現行のカメラ状態を退避する。
    savedView_ = mainCamera->GetViewMatrix();
    savedPosition_ = mainCamera->GetPosition();

    Vector3 reflectedPos = savedPosition_;
    reflectedPos.y = 2.0f * waterHeight - savedPosition_.y;

    // Camera 実装へ一時的に反射ビュー行列を適用する。
    auto* castableCamera = dynamic_cast<CoreEngine::Camera*>(mainCamera);
    if (castableCamera) {
        castableCamera->SetViewMatrix(reflectedView);
        castableCamera->SetTranslate(reflectedPos);
        castableCamera->TransferMatrix();
        hasSavedCameraState_ = true;
    } else {
        hasSavedCameraState_ = false;
    }
}

void WaterReflectionPass::RestoreMainCamera(ICamera* mainCamera)
{
    clipEnabled_ = false;
    if (!mainCamera || !hasSavedCameraState_) {
        return;
    }

    auto* castableCamera = dynamic_cast<CoreEngine::Camera*>(mainCamera);
    if (castableCamera) {
        castableCamera->SetViewMatrix(savedView_);
        castableCamera->SetTranslate(savedPosition_);
        castableCamera->ClearExternalViewMatrix();
        castableCamera->UpdateMatrix();
    }
    hasSavedCameraState_ = false;
}

ReflectionViewResult WaterReflectionPass::BuildResult(
    D3D12_GPU_DESCRIPTOR_HANDLE reflectionSrv,
    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
    D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv) const
{
    // Engine 側で生成済みの ReflectionView 出力を、水面描画向けの結果へ束ねる。
    ReflectionViewResult result{};
    result.reflectionSrv = reflectionSrv;
    result.sceneDepthSrv = sceneDepthSrv;
    result.sceneColorSrv = sceneColorSrv;
    result.clipPlane = clipPlane_;
    result.isValid = result.reflectionSrv.ptr != 0;
    return result;
}

Matrix4x4 WaterReflectionPass::CalcReflectedViewMatrix(
    ICamera* mainCamera,
    float waterHeight) const
{
    // 水面平面 Y = waterHeight に対してカメラビュー行列を反転する
    // XMMatrixReflect を使用して反射行列を計算し、ビュー行列に乗算する

    // 水面平面: Y 軸正方向の法線、水面高さ = waterHeight
    // 平面方程式: 0*x + 1*y + 0*z + (-waterHeight) = 0
    XMVECTOR plane = XMVectorSet(0.0f, 1.0f, 0.0f, -waterHeight);
    XMMATRIX reflectMtx = XMMatrixReflect(plane);

    // mainCamera のビュー行列を XMMATRIX に変換
    const Matrix4x4& view = mainCamera->GetViewMatrix();
    XMMATRIX viewMtx = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&view));

    // 反射行列 × ビュー行列
    XMMATRIX reflectedView = XMMatrixMultiply(reflectMtx, viewMtx);

    // Matrix4x4 に変換して返す
    Matrix4x4 result;
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&result), reflectedView);
    return result;
}
