#include "CameraDebugUI.h"
#include "CameraAnimationEditor.h"
#include "CameraInspectorEditor.h"
#include "CameraPresetEditor.h"

#ifdef _DEBUG

#include <imgui.h>
#include <numbers>
#include <algorithm>
#include "Camera/CameraManager.h"
#include "Camera/ICamera.h"
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Release/Camera.h"

using namespace CoreEngine::MathCore;

namespace CoreEngine {

    CameraDebugUI::CameraDebugUI()
        : cameraManager_(nullptr)
        , inspectorEditor_(std::make_unique<CameraInspectorEditor>())
        , presetEditor_(std::make_unique<CameraPresetEditor>())
        , animationEditor_(std::make_unique<CameraAnimationEditor>())
    {
    }

    void CameraDebugUI::Initialize(CameraManager* cameraManager)
    {
        cameraManager_ = cameraManager;
    }

    void CameraDebugUI::Draw()
    {
        if (!cameraManager_) {
            return;
        }

        // アニメーション更新は専用クラスへ委譲する。
        if (animationEditor_) {
            animationEditor_->Update(cameraManager_);
        }

        // 専用のカメラウィンドウを作成
        if (ImGui::Begin("Camera", nullptr, ImGuiWindowFlags_None)) {
            ImGui::Text("登録カメラ数: %zu", cameraManager_->GetCameraCount());
            ImGui::Separator();

            // タブバーで整理
            if (ImGui::BeginTabBar("CameraTabs")) {

                // 3Dカメラタブ
                if (ImGui::BeginTabItem("3D Camera")) {
                    DrawCamera3DSection();
                    ImGui::EndTabItem();
                }

                // 2Dカメラタブ
                if (ImGui::BeginTabItem("2D Camera")) {
                    DrawCamera2DSection();
                    ImGui::EndTabItem();
                }

                // プリセットタブ
                if (ImGui::BeginTabItem("Presets")) {
                    DrawPresetSection();
                    ImGui::EndTabItem();
                }

                // アニメーションタブ
                if (ImGui::BeginTabItem("Animation")) {
                    DrawAnimationSection();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    void CameraDebugUI::DrawCamera3DSection()
    {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "3Dカメラ");
        ImGui::Separator();

        // 3Dカメラ一覧と切り替え
        bool has3DCamera = false;
        const auto& allCameras = cameraManager_->GetAllCameras();
        std::string activeName = cameraManager_->GetActiveCameraName(CameraType::Camera3D);

        // カメラ切り替えラジオボタン
        for (const auto& [name, camera] : allCameras) {
            if (camera->GetCameraType() == CameraType::Camera3D) {
                has3DCamera = true;
                bool isActive = (name == activeName);
                if (ImGui::RadioButton(name.c_str(), isActive)) {
                    cameraManager_->SetActiveCamera(name, CameraType::Camera3D);
                }
            }
        }

        if (!has3DCamera) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "3Dカメラが登録されていません");
            return;
        }

        ImGui::Separator();

        // アクティブカメラの制御
        ICamera* activeCamera = cameraManager_->GetActiveCamera(CameraType::Camera3D);
        if (!activeCamera) return;

        ImGui::Text("アクティブ: %s", activeName.c_str());

        // カメラ基本情報は専用インスペクターへ委譲する。
        if (inspectorEditor_) {
            inspectorEditor_->DrawCameraBasicInfo(activeCamera);
        }

        ImGui::Separator();

        // カメラタイプ別のコントロール
        DebugCamera* debugCam = dynamic_cast<DebugCamera*>(activeCamera);
        if (debugCam && inspectorEditor_) {
            inspectorEditor_->DrawDebugCameraInspector(debugCam);
        } else {
            Camera* releaseCam = dynamic_cast<Camera*>(activeCamera);
            if (releaseCam && inspectorEditor_) {
                inspectorEditor_->DrawReleaseCameraInspector(releaseCam);
            }
        }
    }

    void CameraDebugUI::DrawCamera2DSection()
    {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "2Dカメラ");
        ImGui::Separator();

        // 2Dカメラ一覧と切り替え
        bool has2DCamera = false;
        const auto& allCameras = cameraManager_->GetAllCameras();
        std::string activeName = cameraManager_->GetActiveCameraName(CameraType::Camera2D);

        // カメラ切り替えラジオボタン
        for (const auto& [name, camera] : allCameras) {
            if (camera->GetCameraType() == CameraType::Camera2D) {
                has2DCamera = true;
                bool isActive = (name == activeName);
                if (ImGui::RadioButton(name.c_str(), isActive)) {
                    cameraManager_->SetActiveCamera(name, CameraType::Camera2D);
                }
            }
        }

        if (!has2DCamera) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "2Dカメラが登録されていません");
            return;
        }

        ImGui::Separator();

        // アクティブカメラの制御
        ICamera* activeCamera = cameraManager_->GetActiveCamera(CameraType::Camera2D);
        if (!activeCamera) return;

        ImGui::Text("アクティブ: %s", activeName.c_str());
        ImGui::Separator();

        // カメラ基本情報
        if (inspectorEditor_) {
            inspectorEditor_->DrawCameraBasicInfo(activeCamera);
        }
    }

    // ===== プリセット管理セクション =====
    void CameraDebugUI::DrawPresetSection()
    {
        // アクティブカメラを取得
        ICamera* activeCamera = cameraManager_->GetActiveCamera(CameraType::Camera3D);
        if (!activeCamera) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "アクティブな3Dカメラがありません");
            return;
        }

        if (!presetEditor_) {
            return;
        }

        // プリセットUI描画は専用クラスへ委譲する。
        presetEditor_->DrawUI(presetManager_, activeCamera);
    }

    // ===== アニメーションセクション =====
    void CameraDebugUI::DrawAnimationSection()
    {
        if (!animationEditor_) {
            return;
        }
        animationEditor_->DrawUI(presetManager_, cameraManager_);
    }

}

#endif // _DEBUG

