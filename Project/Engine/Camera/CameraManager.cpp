#include "CameraManager.h"

#ifdef _DEBUG
#include <imgui.h>
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Release/Camera.h"
#endif


namespace CoreEngine
{
void CameraManager::RegisterCamera(const std::string& name, std::unique_ptr<ICamera> camera)
{
	if (!camera) {
		return;
	}

	CameraType cameraType = camera->GetCameraType();

	// 既存の同名カメラがあれば削除
	if (cameras_.find(name) != cameras_.end()) {
		// アクティブカメラだった場合はクリア
		if (cameraType == CameraType::Camera3D && activeCamera3DName_ == name) {
			activeCamera3DName_.clear();
			activeCamera3D_ = nullptr;
		}
		else if (cameraType == CameraType::Camera2D && activeCamera2DName_ == name) {
			activeCamera2DName_.clear();
			activeCamera2D_ = nullptr;
		}
	}

	cameras_[name] = std::move(camera);

	// タイプごとに最初に登録されたカメラを自動的にアクティブに設定
	if (cameraType == CameraType::Camera3D && !activeCamera3D_) {
		SetActiveCamera(name, CameraType::Camera3D);
	}
	else if (cameraType == CameraType::Camera2D && !activeCamera2D_) {
		SetActiveCamera(name, CameraType::Camera2D);
	}
}

void CameraManager::UnregisterCamera(const std::string& name)
{
	auto it = cameras_.find(name);
	if (it == cameras_.end()) {
		return;
	}

	CameraType cameraType = it->second->GetCameraType();

	// アクティブカメラだった場合はクリア
	if (cameraType == CameraType::Camera3D && activeCamera3DName_ == name) {
		activeCamera3DName_.clear();
		activeCamera3D_ = nullptr;
	}
	else if (cameraType == CameraType::Camera2D && activeCamera2DName_ == name) {
		activeCamera2DName_.clear();
		activeCamera2D_ = nullptr;
	}

	cameras_.erase(it);
}

bool CameraManager::SetActiveCamera(const std::string& name, CameraType type)
{
	auto it = cameras_.find(name);
	if (it == cameras_.end()) {
		return false;
	}

	// カメラタイプが一致するか確認
	if (it->second->GetCameraType() != type) {
		return false;
	}

	if (type == CameraType::Camera3D) {
		activeCamera3DName_ = name;
		activeCamera3D_ = it->second.get();
	}
	else if (type == CameraType::Camera2D) {
		activeCamera2DName_ = name;
		activeCamera2D_ = it->second.get();
	}

	return true;
}

bool CameraManager::SetActiveCamera(const std::string& name)
{
	// 従来の互換性維持: カメラを検索してタイプに応じて設定
	auto it = cameras_.find(name);
	if (it == cameras_.end()) {
		return false;
	}

	CameraType type = it->second->GetCameraType();
	return SetActiveCamera(name, type);
}

ICamera* CameraManager::GetActiveCamera(CameraType type) const
{
	if (type == CameraType::Camera3D) {
		return activeCamera3D_;
	}
	else if (type == CameraType::Camera2D) {
		return activeCamera2D_;
	}
	return nullptr;
}

ICamera* CameraManager::GetActiveCamera() const
{
	// 従来の互換性維持: Camera3Dを返す
	return activeCamera3D_;
}

ICamera* CameraManager::GetCamera(const std::string& name) const
{
	auto it = cameras_.find(name);
	if (it == cameras_.end()) {
		return nullptr;
	}
	return it->second.get();
}

const Matrix4x4& CameraManager::GetViewMatrix() const
{
	static Matrix4x4 identity = MathCore::Matrix::Identity();
	if (!activeCamera3D_) {
		return identity;
	}
	return activeCamera3D_->GetViewMatrix();
}

const Matrix4x4& CameraManager::GetProjectionMatrix() const
{
	static Matrix4x4 identity = MathCore::Matrix::Identity();
	if (!activeCamera3D_) {
		return identity;
	}
	return activeCamera3D_->GetProjectionMatrix();
}

Vector3 CameraManager::GetCameraPosition() const
{
	if (!activeCamera3D_) {
		return { 0.0f, 0.0f, 0.0f };
	}
	return activeCamera3D_->GetPosition();
}

const std::string& CameraManager::GetActiveCameraName(CameraType type) const
{
	static std::string empty = "";
	if (type == CameraType::Camera3D) {
		return activeCamera3DName_;
	}
	else if (type == CameraType::Camera2D) {
		return activeCamera2DName_;
	}
	return empty;
}

void CameraManager::Update()
{
	// 3Dカメラの更新
	if (activeCamera3D_ && activeCamera3D_->GetActive()) {
		activeCamera3D_->Update();
	}

	// 2Dカメラの更新
	if (activeCamera2D_ && activeCamera2D_->GetActive()) {
		activeCamera2D_->Update();
	}
}

#ifdef _DEBUG
void CameraManager::DrawImGui()
{
	// 専用のカメラウィンドウを作成（ビューポートの下に配置）
	if (ImGui::Begin("Camera", nullptr, ImGuiWindowFlags_None)) {
		ImGui::Text("登録カメラ数: %zu", cameras_.size());
		ImGui::Separator();

		// ===== 3Dカメラセクション =====
		if (ImGui::CollapsingHeader("3D Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
			// 3Dカメラ切り替え
			bool has3DCamera = false;
			for (const auto& [name, camera] : cameras_) {
				if (camera->GetCameraType() == CameraType::Camera3D) {
					has3DCamera = true;
					bool isActive = (name == activeCamera3DName_);
					if (ImGui::RadioButton(name.c_str(), isActive)) {
						SetActiveCamera(name, CameraType::Camera3D);
					}
				}
			}

			if (!has3DCamera) {
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "3Dカメラが登録されていません");
			}
			else if (activeCamera3D_) {
				ImGui::Separator();
				ImGui::Text("アクティブ: %s", activeCamera3DName_.c_str());

				// カメラの有効/無効切り替え
				bool isActive = activeCamera3D_->GetActive();
				if (ImGui::Checkbox("有効##3D", &isActive)) {
					activeCamera3D_->SetActive(isActive);
				}

				// 位置情報
				Vector3 pos = activeCamera3D_->GetPosition();
				ImGui::Text("位置: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);

				// ===== DebugCamera固有のコントロール =====
				DebugCamera* debugCam = dynamic_cast<DebugCamera*>(activeCamera3D_);
				if (debugCam) {
					ImGui::Separator();
					ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "デバッグカメラ制御");

					// 現在の状態表示
					Vector3 target = debugCam->GetTarget();
					float distance = debugCam->GetDistance();
					float pitch = debugCam->GetPitch();
					float yaw = debugCam->GetYaw();

					ImGui::Text("注視点: (%.2f, %.2f, %.2f)", target.x, target.y, target.z);
					ImGui::Text("距離: %.2f", distance);
					ImGui::Text("ピッチ: %.2f° (%.3f rad)", pitch * 180.0f / 3.14159f, pitch);
					ImGui::Text("ヨー: %.2f° (%.3f rad)", yaw * 180.0f / 3.14159f, yaw);

					if (debugCam->IsControlling()) {
						ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "操作中");
					}
				}
			}
		}

		ImGui::Separator();

		// ===== 2Dカメラセクション =====
		if (ImGui::CollapsingHeader("2D Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
			// 2Dカメラ切り替え
			bool has2DCamera = false;
			for (const auto& [name, camera] : cameras_) {
				if (camera->GetCameraType() == CameraType::Camera2D) {
					has2DCamera = true;
					bool isActive = (name == activeCamera2DName_);
					if (ImGui::RadioButton(name.c_str(), isActive)) {
						SetActiveCamera(name, CameraType::Camera2D);
					}
				}
			}

			if (!has2DCamera) {
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "2Dカメラが登録されていません");
			}
			else if (activeCamera2D_) {
				ImGui::Separator();
				ImGui::Text("アクティブ: %s", activeCamera2DName_.c_str());

				// カメラの有効/無効切り替え
				bool isActive = activeCamera2D_->GetActive();
				if (ImGui::Checkbox("有効##2D", &isActive)) {
					activeCamera2D_->SetActive(isActive);
				}

				// 位置情報
				Vector3 pos = activeCamera2D_->GetPosition();
				ImGui::Text("位置: (%.2f, %.2f)", pos.x, pos.y);
			}
		}
	}
	ImGui::End();
}
#endif
}
