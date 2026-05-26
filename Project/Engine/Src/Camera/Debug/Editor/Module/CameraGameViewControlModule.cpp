#include "pch.h"
#include "CameraGameViewControlModule.h"

#ifdef USE_IMGUI

#include "Utility/Debug/ImGui/ImGuiAll.h"

#include "Camera/CameraManager.h"
#include "Camera/Release/Camera.h"
#include "EngineSystem/EngineSystem.h"
#include "Input/InputManager.h"
#include "Input/InputQuery.h"
#include "Input/MouseInput.h"
#include "Utility/FrameRate/FrameRateController.h"

#include <dinput.h>
#include <cmath>
#include <numbers>

namespace CoreEngine
{
    // -----------------------------------------------------------------------
    // ヘルパー：GameビューImGuiウィンドウ内にマウスカーソルがあるか
    // -----------------------------------------------------------------------
    static bool IsMouseInGameWindow()
    {
        ImGuiWindow* win = ImGui::FindWindowByName("Game");
        if (!win) { return false; }
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 mp = io.MousePos;
        return mp.x >= win->Pos.x && mp.x <= win->Pos.x + win->Size.x &&
               mp.y >= win->Pos.y && mp.y <= win->Pos.y + win->Size.y;
    }

    // -----------------------------------------------------------------------
    // Update：キー入力でリリースカメラを移動・回転
    // -----------------------------------------------------------------------
    void CameraGameViewControlModule::Update(const CameraEditorContext& context)
    {
        if (!flightEnabled_) { return; }
        if (!context.cameraManager || !context.engineSystem) { return; }

        // リリースカメラ（Camera）を取得
        Camera* releaseCamera = nullptr;
        for (const auto& [name, cam] : context.cameraManager->GetAllCameras()) {
            if (auto* rc = dynamic_cast<Camera*>(cam.get())) {
                releaseCamera = rc;
                break;
            }
        }
        if (!releaseCamera) { return; }

        // 入力・デルタタイム取得
        auto* inputManager = context.engineSystem->GetComponent<InputManager>();
        auto* frameRate    = context.engineSystem->GetComponent<FrameRateController>();
        if (!inputManager || !frameRate) { return; }

        auto& input = inputManager->GetQuery();
        float dt    = frameRate->GetDeltaTime();

        // ===== 右ドラッグによる視点回転 =====
        // Triggered に頼らず「押されている かつ Gameウィンドウ内で開始」で管理する
        bool rightPressed = input.IsMouseButtonPressed(MouseButton::Right);

        if (rightPressed && !draggingRight_ && IsMouseInGameWindow()) {
            draggingRight_ = true;
        }
        if (!rightPressed) {
            draggingRight_ = false;
        }

        if (draggingRight_) {
            // GetMouseDragX/Y はフレーム間の相対移動量を直接返す
            float dx = static_cast<float>(input.GetMouseDragX());
            float dy = static_cast<float>(input.GetMouseDragY());

            Vector3 rot = releaseCamera->GetRotate();
            rot.y += dx * lookSensitivity_;                               // ヨー
            rot.x += dy * lookSensitivity_ * (invertY_ ? -1.0f : 1.0f);  // ピッチ

            // ピッチ制限
            const float maxPitch = std::numbers::pi_v<float> * 0.49f;
            rot.x = std::clamp(rot.x, -maxPitch, maxPitch);

            releaseCamera->SetRotate(rot);
        }

        // ===== WASD / Q・E による移動 =====
        // GetForward() は -Z 方向を返すため、W で +Z（前進）になるよう符号を反転する
        Vector3 forward = releaseCamera->GetForward();
        Vector3 right   = releaseCamera->GetRight();

        Vector3 move = { 0.0f, 0.0f, 0.0f };

        // W: +Z方向（-forward）、S: -Z方向（+forward）
        if (input.IsKeyPressed(DIK_W)) { move.x -= forward.x; move.y -= forward.y; move.z -= forward.z; }
        if (input.IsKeyPressed(DIK_S)) { move.x += forward.x; move.y += forward.y; move.z += forward.z; }
        if (input.IsKeyPressed(DIK_D)) { move.x += right.x;   move.y += right.y;   move.z += right.z;   }
        if (input.IsKeyPressed(DIK_A)) { move.x -= right.x;   move.y -= right.y;   move.z -= right.z;   }
        if (input.IsKeyPressed(DIK_E)) { move.y += 1.0f; }   // ワールドY軸上昇
        if (input.IsKeyPressed(DIK_Q)) { move.y -= 1.0f; }   // ワールドY軸下降

        // 正規化（ゼロベクトルは無視）
        float len = std::sqrtf(move.x * move.x + move.y * move.y + move.z * move.z);
        if (len > 0.0001f) {
            float spd = moveSpeed_ * dt;
            Vector3 pos = releaseCamera->GetTranslate();
            pos.x += (move.x / len) * spd;
            pos.y += (move.y / len) * spd;
            pos.z += (move.z / len) * spd;
            releaseCamera->SetTranslate(pos);
        }
    }

    // -----------------------------------------------------------------------
    // Draw：エディターUI
    // -----------------------------------------------------------------------
    void CameraGameViewControlModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) { return; }

        // リリースカメラを取得
        Camera* releaseCamera = nullptr;
        std::string releaseCameraName;
        for (const auto& [name, cam] : context.cameraManager->GetAllCameras()) {
            if (auto* rc = dynamic_cast<Camera*>(cam.get())) {
                releaseCamera = rc;
                releaseCameraName = name;
                break;
            }
        }

        if (!releaseCamera) {
            UI::Hint("リリースカメラ（Camera）が登録されていません。");
            return;
        }

        ImGui::Text("対象カメラ: %s", releaseCameraName.c_str());
        UI::Separator();

        // ===== フライトモード トグル =====
        UI::SectionHeader("Gameビュー自由操作");

        if (UI::Widgets::ToggleSwitch("フライトモード", &flightEnabled_)) {
            // 無効化時はドラッグ状態をリセット
            if (!flightEnabled_) {
                draggingRight_ = false;
            }
        }

        if (flightEnabled_) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[ フライトモード ON ]");
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[ フライトモード OFF ]");
        }

        UI::Hint("W/S = 前後  A/D = 左右  Q/E = 下上\n右ドラッグ = 視点回転");

        if (!flightEnabled_) { return; }

        // ===== 操作設定 =====
        UI::SectionHeader("操作設定");
        UI::DragFloat("移動速度", moveSpeed_, 0.5f, 0.1f, 500.0f, "%.1f");
        UI::DragFloat("回転感度", lookSensitivity_, 0.0001f, 0.0001f, 0.05f, "%.4f");

        bool inv = invertY_;
        if (UI::Widgets::ToggleSwitch("Y軸反転", &inv)) {
            invertY_ = inv;
        }

        // ===== 現在のカメラ状態 =====
        UI::SectionHeader("カメラ情報");
        Vector3 pos = releaseCamera->GetTranslate();
        Vector3 rot = releaseCamera->GetRotate();
        ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
        ImGui::Text("Rotation: %.2f, %.2f, %.2f", rot.x, rot.y, rot.z);

        UI::Separator();
        if (ImGui::Button("カメラをリセット")) {
            releaseCamera->Reset();
            draggingRight_ = false;
        }
    }
}

#endif // USE_IMGUI
