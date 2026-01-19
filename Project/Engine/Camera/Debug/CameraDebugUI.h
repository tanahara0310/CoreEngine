#pragma once

#ifdef _DEBUG

#include <string>
#include <vector>
#include <Math/MathCore.h>
#include "Camera/CameraStructs.h"
#include "Camera/CameraPresetManager.h"

namespace CoreEngine {

	// 前方宣言
	class CameraManager;
	class ICamera;
	class DebugCamera;
	class Camera;

	/// @brief カメラデバッグUI - ImGuiを使用したカメラ制御インターフェース
	class CameraDebugUI {
	public:

		/// @brief コンストラクタ
		CameraDebugUI();

		/// @brief 初期化
		/// @param cameraManager カメラマネージャーへのポインタ
		void Initialize(CameraManager* cameraManager);

		/// @brief ImGuiウィンドウを描画
		void Draw();

	private:
		/// @brief 3Dカメラセクションを描画
		void DrawCamera3DSection();

		/// @brief 2Dカメラセクションを描画
		void DrawCamera2DSection();

		/// @brief DebugCamera専用コントロールを描画
		/// @param debugCamera デバッグカメラへのポインタ
		void DrawDebugCameraControls(DebugCamera* debugCamera);

		/// @brief Releaseカメラ専用コントロールを描画
		/// @param camera リリースカメラへのポインタ
		void DrawReleaseCameraControls(Camera* camera);

		/// @brief カメラ基本情報を描画
		/// @param camera カメラへのポインタ
		void DrawCameraBasicInfo(ICamera* camera);

	/// @brief カメラ方向ベクトル情報を描画
	/// @param camera リリースカメラへのポインタ
	void DrawCameraVectorInfo(Camera* camera);

	/// @brief プリセット管理セクションを描画
	void DrawPresetSection();

	/// @brief アニメーションセクションを描画
	void DrawAnimationSection();

	/// @brief アニメーション開始
	/// @param fromIndex 開始スナップショットのインデックス
	/// @param toIndex 終了スナップショットのインデックス
	void StartAnimation(int fromIndex, int toIndex);

	/// @brief アニメーション更新
	void UpdateAnimation();

private:
	CameraManager* cameraManager_ = nullptr;

	// UI状態
	bool showAdvancedSettings_ = false;

	// リリースカメラ用のLookAtモード
	bool releaseCameraLookAtMode_ = false;
	Vector3 releaseCameraLookAtTarget_;

	// プリセットマネージャー
	CameraPresetManager presetManager_;

	// アニメーション用JSONファイル選択
	int animFromFileIndex_ = 0;
	int animToFileIndex_ = 0;

	// アニメーション
	struct AnimationState {
		bool isAnimating = false;
		float progress = 0.0f;
		float duration = 1.0f;
		CameraSnapshot fromSnapshot;
		CameraSnapshot toSnapshot;
	};
	AnimationState animation_;

	// アニメーション設定UI
	float animDuration_ = 1.0f;
	int easingTypeIndex_ = 0;  // 0=Linear, 1=EaseInOut, 2=EaseIn, 3=EaseOut
};

}

#endif // _DEBUG
