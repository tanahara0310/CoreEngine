#include "pch.h"
#include "Camera.h"
#include "Math/MathCore.h"
#include <WinApp/WinApp.h>
#include <algorithm>
#include <cmath>

namespace CoreEngine
{

using namespace CoreEngine::MathCore;

void Camera::Initialize(UploadRing* uploadRing)
{
    if (uploadRing) {
        gpuConstants_.Initialize(*uploadRing);
    }

    UpdateMatrix();
}

void Camera::Update()
{
    UpdateMatrix();
}

void Camera::UpdateMatrix()
{
    RebuildMatrices();
    TransferMatrix();
}

float Camera::ResolveAspectRatio() const
{
    if (parameters_.aspectRatio > 0.0f) {
        return parameters_.aspectRatio;
    }
    // ウィンドウのクライアント領域ではなく基準解像度に固定する。
    // クライアント領域に追従すると、フルスクリーン解除で画角（見える範囲）が変わる。
    // 縦横比の差は表示側がレターボックスで吸収する
    return WinApp::GetReferenceAspect();
}

void Camera::RebuildMatrices()
{
    cameraMatrix_ = Matrix::MakeAffine(scale_, rotate_, translate_);
    viewMatrix_ = Matrix::Inverse(cameraMatrix_);

    if (parameters_.IsOrthographic()) {
        // 見える高さは orthographicSize の 2 倍、幅は縦横比から決める
        const float halfHeight = (std::max)(parameters_.orthographicSize, 1.0e-4f);
        const float halfWidth = halfHeight * ResolveAspectRatio();
        projectionMatrix_ = Rendering::Orthographic(
            -halfWidth, halfHeight, halfWidth, -halfHeight,
            parameters_.nearClip, parameters_.farClip);
        return;
    }

    projectionMatrix_ = Rendering::PerspectiveFov(
        parameters_.fov,
        ResolveAspectRatio(),
        parameters_.nearClip,
        parameters_.farClip);
}

void Camera::TransferMatrix()
{
    gpuConstants_.Set(CameraForGPU{ GetPosition() });
}

void Camera::SetProjectionJitter(float ndcX, float ndcY)
{
    projectionJitterX_ = ndcX;
    projectionJitterY_ = ndcY;
    projectionJitterActive_ = (ndcX != 0.0f) || (ndcY != 0.0f);

    if (!projectionJitterActive_) {
        return;
    }

    // 行ベクトル規約（clip = v * P）。透視射影では clip.w にビュー空間 z が入るため、
    // m[2][0] / m[2][1] への加算は「clip.xy += ndc * clip.w」と等価になり、
    // 深度に関わらず一定のサブピクセルずれになる。平行投影では clip.w が 1 なので平行移動の行へ足す
    const int row = parameters_.IsOrthographic() ? 3 : 2;
    jitteredProjectionMatrix_ = projectionMatrix_;
    jitteredProjectionMatrix_.m[row][0] += ndcX;
    jitteredProjectionMatrix_.m[row][1] += ndcY;
}

void Camera::LookAt(const Vector3& target)
{
    const Vector3 forward = Normalize(target - translate_);

    // MakeAffine の回転は Rx * Ry * Rz（行ベクトル規約）。roll = 0 のとき
    // 第 3 行（前方軸）は (cosX sinY, -sinX, cosX cosY) になるので、
    // yaw = atan2(f.x, f.z) / pitch = asin(-f.y) が LookAt と厳密に一致する。
    rotate_ = { std::asinf(-forward.y), std::atan2f(forward.x, forward.z), 0.0f };
}

Vector3 Camera::GetForward() const
{
    // 従来通り「カメラ行列の Z 軸の逆」を返す（呼び出し側がこの符号前提で組まれている）
    return CoreEngine::Normalize(Vector3{
        -cameraMatrix_.m[2][0],
        -cameraMatrix_.m[2][1],
        -cameraMatrix_.m[2][2]
    });
}

Vector3 Camera::GetRight() const
{
    return CoreEngine::Normalize(Vector3{
        cameraMatrix_.m[0][0],
        cameraMatrix_.m[0][1],
        cameraMatrix_.m[0][2]
    });
}

Vector3 Camera::GetUp() const
{
    return CoreEngine::Normalize(Vector3{
        cameraMatrix_.m[1][0],
        cameraMatrix_.m[1][1],
        cameraMatrix_.m[1][2]
    });
}

void Camera::Reset()
{
    scale_ = { 1.0f, 1.0f, 1.0f };
    rotate_ = { 0.0f, 0.0f, 0.0f };
    translate_ = { 0.0f, 0.0f, -10.0f };
    parameters_.Reset();
    RebuildMatrices();
}

CameraSnapshot Camera::CaptureSnapshot(const std::string& name) const
{
    CameraSnapshot snapshot;
    snapshot.name = name;
    snapshot.position = translate_;
    snapshot.rotation = rotate_;
    snapshot.scale = scale_;
    snapshot.parameters = parameters_;
    return snapshot;
}

void Camera::RestoreSnapshot(const CameraSnapshot& snapshot)
{
    translate_ = snapshot.position;
    rotate_ = snapshot.rotation;
    scale_ = snapshot.scale;
    parameters_ = snapshot.parameters;
    RebuildMatrices();
}

}
