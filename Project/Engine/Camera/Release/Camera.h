#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "MathCore.h"
#include "Structs/TransformationMatrix.h"
#include "Camera/ICamera.h"
#include "Camera/CameraStructs.h"

/// <summary>
/// カメラクラス
/// </summary>

namespace CoreEngine
{
class Camera : public ICamera {
public:
	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize(ID3D12Device* device);

	/// <summary>
	/// 行列の更新
	/// </summary>
	void UpdateMatrix();

	// ====== ICamera インターフェースの実装 ======

	/// @brief カメラの更新（ICamera から）
	void Update() override {
		UpdateMatrix();
	}

	/// @brief ビューマトリックスの取得（ICamera から）
	const Matrix4x4& GetViewMatrix() const override { return viewMatrix_; }

	/// @brief プロジェクションマトリックスの取得（ICamera から）
	const Matrix4x4& GetProjectionMatrix() const override { return projectionMatrix_; }

	/// @brief カメラの位置取得（ICamera から）
	Vector3 GetPosition() const override { return translate_; }

	/// @brief カメラのGPU仮想アドレスを取得（ICamera から）
	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const override {
		return cameraGPUResource_ ? cameraGPUResource_->GetGPUVirtualAddress() : 0;
	}

	/// @brief 行列をGPUに転送（ICamera から）
	void TransferMatrix() override;

	/// @brief カメラのタイプを取得
	CameraType GetCameraType() const override { return CameraType::Camera3D; }

	// ====== Camera 固有のアクセッサ ======

	void SetScale(const Vector3& scale) { this->scale_ = scale; }
	void SetRotate(const Vector3& rotate) { this->rotate_ = rotate; }
	void SetTranslate(const Vector3& translate) { this->translate_ = translate; }

	Vector3 GetScale() const { return scale_; }
	Vector3 GetRotate() const { return rotate_; }
	Vector3 GetTranslate() const { return translate_; }

	const Matrix4x4& GetCameraMatrix() const { return cameraMatrix_; }

	void SetViewMatrix(const Matrix4x4& viewMatrix)
	{
		this->viewMatrix_ = viewMatrix;
		useExternalViewMatrix_ = true;
	}

private:
	Vector3 scale_ = { 1.0f, 1.0f, 1.0f }; // スケール
	Vector3 rotate_ = { 0.0f, 0.0f, 0.0f }; // 回転
	Vector3 translate_ = { 0.0f, 0.0f, -10.0f }; // 平行移動

	Matrix4x4 viewMatrix_; // ビュー行列
	Matrix4x4 projectionMatrix_; // 射影行列
	Matrix4x4 cameraMatrix_; // カメラ行列

	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
	TransformationMatrix* cameraData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> cameraGPUResource_;
	CameraForGPU* cameraGPUData_ = nullptr;

	bool useExternalViewMatrix_ = false; // 外部ビュー行列を使用するかどうか
};
}
