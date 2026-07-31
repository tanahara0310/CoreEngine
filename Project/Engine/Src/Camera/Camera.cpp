#include "pch.h"
#include "Camera.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Math/MathCore.h"
#include <WinApp/WinApp.h>
#include <cmath>

namespace CoreEngine
{

using namespace CoreEngine::MathCore;

void Camera::Initialize(ID3D12Device* device)
{
    if (device) {
        cameraGPUResource_ = ResourceFactory::CreateBufferResource(device, sizeof(CameraForGPU));
        cameraGPUResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraGPUData_));
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
    const float height = static_cast<float>(WinApp::GetCurrentClientHeightStatic());
    if (height <= 0.0f) {
        return 1.0f;
    }
    return static_cast<float>(WinApp::GetCurrentClientWidthStatic()) / height;
}

void Camera::RebuildMatrices()
{
    if (parameters_.projectionType == CameraProjectionType::Orthographic) {
        // ===== 2D（正射影）=====
        // ビュー行列: ズーム → Z 回転 → カメラ位置の逆平行移動
        const Matrix4x4 scaleMatrix = Matrix::Scale({ scale_.x, scale_.y, 1.0f });
        const Matrix4x4 rotationMatrix = Matrix::RotationZ(rotate_.z);
        const Matrix4x4 translationMatrix = Matrix::Translation({ -translate_.x, -translate_.y, 0.0f });

        viewMatrix_ = Matrix::Multiply(
            translationMatrix,
            Matrix::Multiply(rotationMatrix, scaleMatrix));

        // カメラ行列は 3D 用の派生（GetForward 等）のためだけに保持する
        cameraMatrix_ = Matrix::Inverse(viewMatrix_);

        // 画面中央を原点、Y軸は上が正（top/bottom を入れ替えて実現）
        const float screenWidth = static_cast<float>(WinApp::GetCurrentClientWidthStatic());
        const float screenHeight = static_cast<float>(WinApp::GetCurrentClientHeightStatic());
        projectionMatrix_ = Rendering::Orthographic(
            -screenWidth * 0.5f,
            screenHeight * 0.5f,
            screenWidth * 0.5f,
            -screenHeight * 0.5f,
            parameters_.nearClip,
            parameters_.farClip);
        return;
    }

    // ===== 3D（透視投影）=====
    cameraMatrix_ = Matrix::MakeAffine(scale_, rotate_, translate_);
    viewMatrix_ = Matrix::Inverse(cameraMatrix_);

    projectionMatrix_ = Rendering::PerspectiveFov(
        parameters_.fov,
        ResolveAspectRatio(),
        parameters_.nearClip,
        parameters_.farClip);
}

void Camera::TransferMatrix()
{
    if (cameraGPUData_) {
        cameraGPUData_->worldPosition = GetPosition();
    }
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
    // 深度に関わらず一定のサブピクセルずれになる。
    jitteredProjectionMatrix_ = projectionMatrix_;
    jitteredProjectionMatrix_.m[2][0] += ndcX;
    jitteredProjectionMatrix_.m[2][1] += ndcY;
}

void Camera::LookAt(const Vector3& target)
{
    const Vector3 forward = Vector::Normalize(Vector::Subtract(target, translate_));

    // MakeAffine の回転は Rx * Ry * Rz（行ベクトル規約）。roll = 0 のとき
    // 第 3 行（前方軸）は (cosX sinY, -sinX, cosX cosY) になるので、
    // yaw = atan2(f.x, f.z) / pitch = asin(-f.y) が LookAt と厳密に一致する。
    rotate_ = { std::asinf(-forward.y), std::atan2f(forward.x, forward.z), 0.0f };
}

Vector3 Camera::GetForward() const
{
    // 従来通り「カメラ行列の Z 軸の逆」を返す（呼び出し側がこの符号前提で組まれている）
    return Vector::Normalize(Vector3{
        -cameraMatrix_.m[2][0],
        -cameraMatrix_.m[2][1],
        -cameraMatrix_.m[2][2]
    });
}

Vector3 Camera::GetRight() const
{
    return Vector::Normalize(Vector3{
        cameraMatrix_.m[0][0],
        cameraMatrix_.m[0][1],
        cameraMatrix_.m[0][2]
    });
}

Vector3 Camera::GetUp() const
{
    return Vector::Normalize(Vector3{
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
