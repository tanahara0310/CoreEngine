#include "Camera.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Math/MathCore.h"
#include <WinApp/WinApp.h>
#include <cmath>

namespace CoreEngine
{

// 新しい数学ライブラリを使用
using namespace CoreEngine::MathCore;

/// 初期化
void Camera::Initialize(ID3D12Device* device)
{
    // カメラのリソースを生成
    cameraResource_ = ResourceFactory::CreateBufferResource(device, sizeof(TransformationMatrix));
    // マッピングしてデータを書き込む
    cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));

    // CameraForGPU用の定数バッファを初期化
    cameraGPUResource_ = ResourceFactory::CreateBufferResource(device, sizeof(CameraForGPU));
    // マッピングしてデータを書き込む
    cameraGPUResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraGPUData_));
}

// カメラの更新
void Camera::UpdateMatrix()
{
    // カメラ行列を計算
    cameraMatrix_ = Matrix::MakeAffine(scale_, rotate_, translate_);
    // ビュー行列を計算（外部ビュー行列を使用しない場合のみ）
    if (!useExternalViewMatrix_) {
        viewMatrix_ = Matrix::Inverse(cameraMatrix_);
    }

    // アスペクト比の計算（パラメータで指定されていない場合は自動計算）
    float aspectRatio = parameters_.aspectRatio;
    if (aspectRatio <= 0.0f) {
        aspectRatio = static_cast<float>(WinApp::GetCurrentClientWidthStatic()) /
            static_cast<float>(WinApp::GetCurrentClientHeightStatic());
    }

    // プロジェクション行列をパラメータから初期化
    projectionMatrix_ = Rendering::PerspectiveFov(
        parameters_.fov,
        aspectRatio,
        parameters_.nearClip,
        parameters_.farClip
    );

    // カメラの行列を転送
    TransferMatrix();
}

// カメラの行列を転送
void Camera::TransferMatrix()
{
    if (!cameraData_)
        return;
    // カメラの行列を定数バッファに転送
    cameraData_->world = Matrix::Identity();
    cameraData_->WVP = Matrix::Multiply(viewMatrix_, projectionMatrix_);

    // カメラ座標の転送（CameraForGPU）
    if (cameraGPUData_) {
        cameraGPUData_->worldPosition = translate_;
    }
}

// LookAt機能: 指定した位置を注視するようにカメラを回転
void Camera::LookAt(const Vector3& target)
{
    // カメラから注視点への方向ベクトル
    Vector3 forward = Vector::Normalize(Vector::Subtract(target, translate_));

    // Y軸周りの回転（ヨー角）を計算
    float yaw = std::atan2f(forward.x, forward.z);

    // X軸周りの回転（ピッチ角）を計算
    float pitch = std::asinf(-forward.y);

    // 回転を設定（ロールは0）
    rotate_ = { pitch, yaw, 0.0f };
}

// カメラの前方向ベクトルを取得
Vector3 Camera::GetForward() const
{
    // カメラ行列から前方向ベクトルを抽出（-Z軸方向）
    return Vector::Normalize(Vector3{
        -cameraMatrix_.m[2][0],
        -cameraMatrix_.m[2][1],
        -cameraMatrix_.m[2][2]
    });
}

// カメラの右方向ベクトルを取得
Vector3 Camera::GetRight() const
{
    // カメラ行列から右方向ベクトルを抽出（X軸方向）
    return Vector::Normalize(Vector3{
        cameraMatrix_.m[0][0],
        cameraMatrix_.m[0][1],
        cameraMatrix_.m[0][2]
    });
}

// カメラの上方向ベクトルを取得
Vector3 Camera::GetUp() const
{
    // カメラ行列から上方向ベクトルを抽出（Y軸方向）
    return Vector::Normalize(Vector3{
        cameraMatrix_.m[1][0],
        cameraMatrix_.m[1][1],
        cameraMatrix_.m[1][2]
    });
}

// カメラをリセット
void Camera::Reset()
{
    scale_ = { 1.0f, 1.0f, 1.0f };
    rotate_ = { 0.0f, 0.0f, 0.0f };
    translate_ = { 0.0f, 0.0f, -10.0f };
    parameters_.Reset();
    useExternalViewMatrix_ = false;
}

// 現在の状態をスナップショットとして保存
CameraSnapshot Camera::CaptureSnapshot(const std::string& name) const
{
    CameraSnapshot snapshot;
    snapshot.name = name;
    snapshot.isDebugCamera = false;

    // Transform情報
    snapshot.position = translate_;
    snapshot.rotation = rotate_;
    snapshot.scale = scale_;

    // カメラパラメータ
    snapshot.parameters = parameters_;

    return snapshot;
}

// スナップショットから状態を復元
void Camera::RestoreSnapshot(const CameraSnapshot& snapshot)
{
    // DebugCamera用のスナップショットは無視
    if (snapshot.isDebugCamera) {
        return;
    }

    // Transform情報を復元
    translate_ = snapshot.position;
    rotate_ = snapshot.rotation;
    scale_ = snapshot.scale;

    // カメラパラメータを復元
    parameters_ = snapshot.parameters;

    // 外部ビュー行列フラグをリセット
    useExternalViewMatrix_ = false;
}
}
