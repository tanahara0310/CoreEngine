#include "pch.h"
#include "CameraGameViewControlModule.h"

#ifdef USE_IMGUI

#include "Editor/ImGui/ImGuiAll.h"

#include "Camera/CameraManager.h"
#include "Camera/Control/FreeLookController.h"
#include "Camera/Control/OrbitFlyController.h"

namespace CoreEngine
{
    void CameraGameViewControlModule::Update(const CameraEditorContext& context)
    {
        // 入力処理はコントローラ側（CameraManager::Update）に一本化済み。
        // 以前はこのモジュールが独自に右ドラッグ・WASD を処理しており、
        // DebugCamera 内蔵の操作や追従モジュールと同じカメラを奪い合っていた。
        (void)context;
    }

    void CameraGameViewControlModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) { return; }

        Camera* activeCamera = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (!activeCamera) {
            UI::Hint("アクティブな3Dカメラがありません。");
            return;
        }

        const std::string& activeName =
            context.cameraManager->GetActiveCameraName(CameraType::Camera3D);
        ImGui::Text("対象カメラ: %s", activeName.c_str());

        ICameraController* controller = context.cameraManager->GetActiveController();
        if (!controller) {
            UI::Separator();
            UI::Hint("このカメラには操作コントローラが付いていません。\n"
                     "Transform タブから直接編集してください。");
            return;
        }

        ImGui::Text("操作方法: %s", controller->GetDisplayName());
        UI::Separator();

        // ===== 自由移動（フライ） =====
        if (auto* freeLook = context.cameraManager->GetActiveControllerAs<FreeLookController>()) {
            UI::SectionHeader("Gameビュー自由操作");

            bool flightEnabled = freeLook->IsEnabled();
            if (UI::Widgets::ToggleSwitch("フライトモード", &flightEnabled)) {
                freeLook->SetEnabled(flightEnabled);
            }
            if (flightEnabled) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[ フライトモード ON ]");
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[ フライトモード OFF ]");
            }

            UI::SectionHeader("操作設定");
            auto settings = freeLook->GetSettings();
            bool changed = false;
            changed |= UI::DragFloat("移動速度", settings.moveSpeed, 0.5f, 0.1f, 500.0f, "%.1f");
            changed |= UI::DragFloat("加速倍率", settings.boostMultiplier, 0.1f, 1.0f, 20.0f, "%.1f");
            changed |= UI::DragFloat("回転感度", settings.lookSensitivity, 0.0001f, 0.0001f, 0.05f, "%.4f");

            bool inv = settings.invertY;
            if (UI::Widgets::ToggleSwitch("Y軸反転", &inv)) {
                settings.invertY = inv;
                changed = true;
            }
            if (changed) {
                freeLook->SetSettings(settings);
            }

            UI::Hint("W/S = 前後  A/D = 左右  Q/E = 下上  Shift = 加速\n右ドラッグ = 視点回転");
        }

        // ===== 軌道操作（Blender 風） =====
        if (auto* orbit = context.cameraManager->GetActiveControllerAs<OrbitFlyController>()) {
            UI::SectionHeader("操作設定");
            auto settings = orbit->GetSettings();
            bool changed = false;
            changed |= UI::DragFloat("回転感度", settings.rotationSensitivity, 0.0001f, 0.0001f, 0.05f, "%.4f");
            changed |= UI::DragFloat("パン感度", settings.panSensitivity, 0.005f, 0.001f, 5.0f, "%.3f");
            changed |= UI::DragFloat("ズーム量", settings.zoomSensitivity, 0.1f, 0.01f, 100.0f, "%.2f");
            changed |= UI::DragFloat("移動速度", settings.flySpeed, 0.5f, 0.1f, 500.0f, "%.1f");
            changed |= UI::DragFloat("加速倍率", settings.flySpeedBoost, 0.1f, 1.0f, 20.0f, "%.1f");

            bool inv = settings.invertY;
            if (UI::Widgets::ToggleSwitch("Y軸反転", &inv)) {
                settings.invertY = inv;
                changed = true;
            }
            bool smooth = settings.smoothMovement;
            if (UI::Widgets::ToggleSwitch("スムーズ移動", &smooth)) {
                settings.smoothMovement = smooth;
                changed = true;
            }
            if (smooth) {
                changed |= UI::SliderFloat("スムージング係数", settings.smoothingFactor, 0.01f, 1.0f, "%.2f");
            }
            if (changed) {
                orbit->SetSettings(settings);
            }

            UI::Hint("中ドラッグ = 回転  Shift+中ドラッグ = パン  ホイール = ズーム\n"
                     "W/A/S/D = 移動  Q/E = 下上  Shift = 加速");
        }

        // ===== 現在のカメラ状態 =====
        UI::SectionHeader("カメラ情報");
        const Vector3 pos = activeCamera->GetTranslate();
        const Vector3 rot = activeCamera->GetRotate();
        ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
        ImGui::Text("Rotation: %.2f, %.2f, %.2f", rot.x, rot.y, rot.z);

        UI::Separator();
        if (ImGui::Button("カメラをリセット")) {
            controller->Reset();
            activeCamera->Reset();
        }
    }
}

#endif // USE_IMGUI
