#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "MathCore.h"
#include "Camera/ICamera.h"

/// @brief 2D用カメラクラス（正射影、深度なし）

namespace CoreEngine
{
class Camera2D : public ICamera {
public:
	/// @brief コンストラクタ（スクリーンサイズはWinAppから自動取得）
	Camera2D();

	/// @brief デストラクタ
	~Camera2D() override = default;

	/// @brief 行列の更新
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
	Vector3 GetPosition() const override { return position_; }

	/// @brief カメラのGPU仮想アドレスを取得（ICamera から）
	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const override {
		return 0; // 2Dカメラは定数バッファ不要
	}

	/// @brief 行列をGPUに転送（ICamera から）
	void TransferMatrix() override {
		// 2Dカメラは行列転送不要（レンダラー側でWVP計算）
	}

	/// @brief カメラのタイプを取得
	CameraType GetCameraType() const override { return CameraType::Camera2D; }

	// ====== Camera2D 固有のアクセッサ ======

	/// @brief カメラの位置を設定
	void SetPosition(const Vector2& position) { this->position_ = { position.x, position.y, 0.0f }; }
	void SetPosition(const Vector3& position) { this->position_ = position; }

	/// @brief カメラの回転を設定（Z軸回転のみ）
	void SetRotation(float rotation) { this->rotation_ = rotation; }

	/// @brief カメラのズーム（スケール）を設定
	void SetZoom(float zoom) { this->zoom_ = zoom; }

	/// @brief 位置を取得
	Vector2 GetPosition2D() const { return { position_.x, position_.y }; }

	/// @brief 回転を取得
	float GetRotation() const { return rotation_; }

	/// @brief ズームを取得
	float GetZoom() const { return zoom_; }

	/// @brief スクリーンサイズを取得
	Vector2 GetScreenSize() const { return { screenWidth_, screenHeight_ }; }

	/// @brief スクリーンサイズを設定（リサイズ対応）
	void SetScreenSize(float width, float height);

private:
	Vector3 position_ = { 0.0f, 0.0f, 0.0f }; // カメラ位置（2D平面）
	float rotation_ = 0.0f;                   // Z軸回転（ラジアン）
	float zoom_ = 1.0f;                       // ズーム倍率

	float screenWidth_;   // スクリーン幅（WinAppから取得）
	float screenHeight_;  // スクリーン高さ（WinAppから取得）

	Matrix4x4 viewMatrix_;                    // ビュー行列
	Matrix4x4 projectionMatrix_;              // 射影行列（正射影）
};
}
