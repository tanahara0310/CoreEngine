#pragma once
#include <Math/MathCore.h>
#include <string>

/// @brief カメラのGPU用構造体

namespace CoreEngine
{

// 前方宣言
enum class CameraType;

/// @brief カメラのGPU用構造体
struct CameraForGPU {
	Vector3 worldPosition; // ワールド座標
};

/// @brief カメラパラメータ構造体
struct CameraParameters {
	// 基本パラメータ
	float fov = 0.45f;                  // 視野角（ラジアン）
	float nearClip = 0.1f;              // ニアクリップ
	float farClip = 1000.0f;            // ファークリップ
	
	// アスペクト比（通常は自動計算されるが、カスタム設定も可能）
	float aspectRatio = 0.0f;           // 0.0f = 自動計算

	/// @brief デフォルトパラメータを取得
	static CameraParameters Default() {
		return CameraParameters{};
	}

	/// @brief パラメータをデフォルトにリセット
	void Reset() {
		*this = Default();
	}

	/// @brief 視野角を度数法で取得
	/// @return 視野角（度）
	float GetFovDegrees() const {
		return fov * 180.0f / 3.14159265359f;
	}

	/// @brief 視野角を度数法で設定
	/// @param degrees 視野角（度）
	void SetFovDegrees(float degrees) {
		fov = degrees * 3.14159265359f / 180.0f;
	}
};

/// @brief カメラスナップショット（プリセット保存用）
struct CameraSnapshot {
	std::string name;                   // プリセット名
	bool isDebugCamera = false;         // DebugCameraかどうか

	// Transform情報（ReleaseCamera用）
	Vector3 position = { 0.0f, 0.0f, -10.0f };
	Vector3 rotation = { 0.0f, 0.0f, 0.0f };
	Vector3 scale = { 1.0f, 1.0f, 1.0f };

	// DebugCamera用パラメータ
	Vector3 target = { 0.0f, 0.0f, 0.0f };
	float distance = 20.0f;
	float pitch = 0.25f;
	float yaw = 3.14159265359f;

	// カメラパラメータ
	CameraParameters parameters;

	/// @brief デフォルトスナップショットを作成
	static CameraSnapshot Default(const std::string& name = "Default") {
		CameraSnapshot snapshot;
		snapshot.name = name;
		return snapshot;
	}
};

}


