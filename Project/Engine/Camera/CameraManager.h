#pragma once

#include "ICamera.h"
#include <memory>
#include <unordered_map>
#include <string>

/// @brief カメラマネージャー - 複数のカメラを管理して動的に切り替えるクラス

namespace CoreEngine
{

#ifdef _DEBUG
	// 前方宣言
	class CameraDebugUI;
#endif

	class CameraManager {
	public:
		/// @brief コンストラクタ
		CameraManager() = default;

		/// @brief デストラクタ
		~CameraManager();

		/// @brief カメラを登録
		/// @param name カメラの名前
		/// @param camera 登録するカメラのユニークポインタ
		void RegisterCamera(const std::string& name, std::unique_ptr<ICamera> camera);

		/// @brief カメラを登録解除
		/// @param name カメラの名前
		void UnregisterCamera(const std::string& name);

		/// @brief アクティブカメラを設定（カメラタイプ別）
		/// @param name カメラの名前
		/// @param type カメラタイプ（3D or 2D）
		/// @return 設定に成功した場合true
		bool SetActiveCamera(const std::string& name, CameraType type);

		/// @brief アクティブカメラを設定（従来の互換性維持版）
		/// @param name カメラの名前
		/// @return 設定に成功した場合true
		bool SetActiveCamera(const std::string& name);

		/// @brief アクティブカメラを取得（カメラタイプ別）
		/// @param type カメラタイプ（3D or 2D）
		/// @return アクティブカメラのポインタ（存在しない場合nullptr）
		ICamera* GetActiveCamera(CameraType type) const;

		/// @brief アクティブカメラを取得（従来の互換性維持版 - Camera3Dを返す）
		/// @return アクティブカメラのポインタ（存在しない場合nullptr）
		ICamera* GetActiveCamera() const;

		/// @brief 名前でカメラを取得
		/// @param name カメラの名前
		/// @return カメラのポインタ（存在しない場合nullptr）
		ICamera* GetCamera(const std::string& name) const;

		/// @brief アクティブカメラのビュー行列を取得（Camera3D）
		/// @return ビュー行列
		const Matrix4x4& GetViewMatrix() const;

		/// @brief アクティブカメラのプロジェクション行列を取得（Camera3D）
		/// @return プロジェクション行列
		const Matrix4x4& GetProjectionMatrix() const;

		/// @brief アクティブカメラの位置を取得（Camera3D）
		/// @return カメラ位置
		Vector3 GetCameraPosition() const;

		/// @brief アクティブカメラを更新（全タイプ）
		void Update();

		/// @brief 登録されているカメラの数を取得
		/// @return カメラの数
		size_t GetCameraCount() const { return cameras_.size(); }

		/// @brief アクティブカメラの名前（Camera3D）を取得
		/// @return アクティブカメラの名前
		const std::string& GetActiveCameraName() const { return activeCamera3DName_; }

		/// @brief アクティブカメラの名前を取得（タイプ別）
		/// @param type カメラタイプ
		/// @return アクティブカメラの名前
		const std::string& GetActiveCameraName(CameraType type) const;

		/// @brief 全カメラのコンテナを取得（デバッグUI用）
		/// @return カメラのコンテナへの参照
		const std::unordered_map<std::string, std::unique_ptr<ICamera>>& GetAllCameras() const { return cameras_; }

#ifdef _DEBUG
		/// @brief ImGuiデバッグウィンドウを描画
		void DrawImGui();
#endif

	private:
		/// @brief カメラのコンテナ
		std::unordered_map<std::string, std::unique_ptr<ICamera>> cameras_;

		/// @brief アクティブなカメラの名前（タイプ別）
		std::string activeCamera3DName_;
		std::string activeCamera2DName_;

		/// @brief アクティブなカメラのポインタ（キャッシュ、タイプ別）
		ICamera* activeCamera3D_ = nullptr;
		ICamera* activeCamera2D_ = nullptr;

#ifdef _DEBUG
		/// @brief デバッグUI（遅延初期化）
		std::unique_ptr<CameraDebugUI> debugUI_;
#endif
	};
}
