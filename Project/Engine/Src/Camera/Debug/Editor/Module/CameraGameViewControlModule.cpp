#include "CameraGameViewControlModule.h"

#ifdef USE_IMGUI

#include "Utility/Debug/ImGui/ImGuiAll.h"

#include "Camera/CameraManager.h"
#include "Camera/Debug/DebugCamera.h"

namespace CoreEngine
{
    void CameraGameViewControlModule::Update(const CameraEditorContext& context)
    {
        (void)context;
    }

    void CameraGameViewControlModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) {
            return;
        }

        // アクティブカメラに依存せず、登録済みカメラから DebugCamera を探す
        DebugCamera* debugCamera = nullptr;
        std::string debugCameraName;
        for (const auto& [name, camera] : context.cameraManager->GetAllCameras()) {
            if (auto* dc = dynamic_cast<DebugCamera*>(camera.get())) {
                debugCamera = dc;
                debugCameraName = name;
                break;
            }
        }

        if (!debugCamera) {
            UI::Hint("DebugCamera が登録されていません。");
            return;
        }

        ImGui::Text("対象カメラ: %s", debugCameraName.c_str());
        UI::Separator();

        DebugCamera::CameraSettings settings = debugCamera->GetSettings();
        bool changed = false;

        // ===== 操作対象ビューの切り替え =====
        UI::SectionHeader("操作対象ビュー");

        bool useGameView = settings.useGameView;
        if (UI::Widgets::ToggleSwitch("Gameビューで操作する", &useGameView)) {
            settings.useGameView = useGameView;
            changed = true;

            // Gameビュー操作が有効になったらオーバーライドを設定し、
            // DebugCamera の視点が Game ビューにも反映されるようにする
            if (useGameView) {
                context.cameraManager->SetGameViewCameraOverride(debugCameraName);
            } else {
                context.cameraManager->SetGameViewCameraOverride("");
            }
        }

        if (settings.useGameView) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[ Gameビューで操作中 ]");
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[ Sceneビューで操作中 ]");
        }
        UI::Hint("中ボタンドラッグ = 回転  /  Shift+中ボタン = パン  /  ホイール = ズーム");

        // ===== 操作感度 =====
        UI::SectionHeader("操作感度");
        changed |= UI::DragFloat("回転感度", settings.rotationSensitivity, 0.0001f, 0.0001f, 0.05f, "%.4f");
        changed |= UI::DragFloat("パン感度",  settings.panSensitivity,      0.00001f, 0.00001f, 0.01f, "%.5f");
        changed |= UI::DragFloat("ズーム感度", settings.zoomSensitivity,     0.1f,    0.1f,    10.0f, "%.2f");

        // ===== 移動設定 =====
        UI::SectionHeader("移動設定");

        bool invertY = settings.invertY;
        if (UI::Widgets::ToggleSwitch("Y軸反転", &invertY)) {
            settings.invertY = invertY;
            changed = true;
        }

        bool smoothMovement = settings.smoothMovement;
        if (UI::Widgets::ToggleSwitch("スムーズ移動", &smoothMovement)) {
            settings.smoothMovement = smoothMovement;
            changed = true;
        }
        if (settings.smoothMovement) {
            changed |= UI::SliderFloat("スムージング係数", settings.smoothingFactor, 0.01f, 1.0f, "%.2f");
        }

        // ===== 距離制限 =====
        UI::SectionHeader("距離制限");
        changed |= UI::DragFloat("最小距離", settings.minDistance, 0.01f,  0.01f,    100.0f,    "%.2f");
        changed |= UI::DragFloat("最大距離", settings.maxDistance, 10.0f,  1.0f,  100000.0f,   "%.1f");

        if (changed) {
            debugCamera->SetSettings(settings);
        }

        UI::Separator();
        if (ImGui::Button("設定をリセット")) {
            DebugCamera::CameraSettings defaultSettings{};
            debugCamera->SetSettings(defaultSettings);
            // リセット時は Gameビュー操作も無効化する
            context.cameraManager->SetGameViewCameraOverride("");
        }
    }
}

#endif // USE_IMGUI
