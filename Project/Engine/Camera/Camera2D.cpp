#include "Camera2D.h"
#include "MathCore.h"
#include "WinApp/WinApp.h"


namespace CoreEngine
{
Camera2D::Camera2D() {
    // WinAppからスクリーンサイズを取得
    screenWidth_ = static_cast<float>(WinApp::kClientWidth);
    screenHeight_ = static_cast<float>(WinApp::kClientHeight);
    
    // 初期状態の行列を更新
    UpdateMatrix();
}

void Camera2D::UpdateMatrix() {
    // ===== ビュー行列の作成 =====
    // 2Dカメラのビュー行列: 位置、回転、ズームを反映
    
    // 1. ズーム（スケール）
    Matrix4x4 scaleMatrix = MathCore::Matrix::Scale({ zoom_, zoom_, 1.0f });
    
    // 2. 回転（Z軸）
    Matrix4x4 rotationMatrix = MathCore::Matrix::RotationZ(rotation_);
    
    // 3. 平行移動（カメラ位置の逆）
    Matrix4x4 translationMatrix = MathCore::Matrix::Translation({ -position_.x, -position_.y, 0.0f });
    
    // ビュー行列 = Translation * Rotation * Scale
    viewMatrix_ = MathCore::Matrix::Multiply(
        translationMatrix,
        MathCore::Matrix::Multiply(rotationMatrix, scaleMatrix)
    );
    
    // ===== 射影行列の作成 =====
    // 2Dカメラは正射影（Orthographic）
    // 画面中央を原点(0,0)とし、Y軸は上が正（top/bottomを入れ替えて実現）
    projectionMatrix_ = MathCore::Rendering::Orthographic(
        -screenWidth_ / 2.0f,   // left
        screenHeight_ / 2.0f,   // top (正の値 - Y軸上が正)
        screenWidth_ / 2.0f,    // right
        -screenHeight_ / 2.0f,  // bottom (負の値 - Y軸上が正)
        0.0f,                    // near
        100.0f                   // far
    );
}

void Camera2D::SetScreenSize(float width, float height) {
    screenWidth_ = width;
    screenHeight_ = height;
    UpdateMatrix();
}
}
