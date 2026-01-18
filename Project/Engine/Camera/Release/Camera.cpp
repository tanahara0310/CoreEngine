#include "Camera.h"
#include "Graphics/Resource/ResourceFactory.h"
#include <MathCore.h>
#include <WinApp/WinApp.h>

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
	// プロジェクション行列を初期化（アスペクト比 = 幅 / 高さ）
	float aspectRatio = static_cast<float>(WinApp::kClientWidth) / static_cast<float>(WinApp::kClientHeight);
	projectionMatrix_ = Rendering::PerspectiveFov(0.45f, aspectRatio, 0.1f, 1000.0f);

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
}
