#include "pch.h"
#include "EditorCameraInput.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/ImGuiAll.h"
#include "EngineSystem/EngineSystem.h"
#include "Input/InputManager.h"
#include "Input/InputQuery.h"
#include "Input/MouseInput.h"

#include <algorithm>
#include <dinput.h>

namespace CoreEngine
{
    namespace {
        /// @brief Gameビューウィンドウ内にカーソルがあるか
        bool IsMouseInGameWindow()
        {
            ImGuiWindow* win = ImGui::FindWindowByName("Game");
            if (!win) {
                return false;
            }
            const ImVec2 mp = ImGui::GetIO().MousePos;
            return mp.x >= win->Pos.x && mp.x <= win->Pos.x + win->Size.x &&
                   mp.y >= win->Pos.y && mp.y <= win->Pos.y + win->Size.y;
        }

        /// @brief カメラ操作を受け付けてよい状態か
        bool IsCameraControlAllowed()
        {
            // ギズモ操作中はカメラを動かさない
            if (ImGuizmo::IsOver() || ImGuizmo::IsUsingAny()) {
                return false;
            }

            // ドッキングのリサイズ・ドラッグ&ドロップ中も奪わない
            const ImGuiMouseCursor mc = ImGui::GetMouseCursor();
            if (mc == ImGuiMouseCursor_ResizeEW
                || mc == ImGuiMouseCursor_ResizeNS
                || mc == ImGuiMouseCursor_Hand
                || ImGui::IsDragDropActive()) {
                return false;
            }

            return IsMouseInGameWindow();
        }
    }

    CameraInputState EditorCameraInput::Collect(EngineSystem* engine)
    {
        CameraInputState state{};

        if (!engine) {
            hasLastCursor_ = false;
            return state;
        }

        auto* inputManager = engine->GetService<InputManager>();
        if (!inputManager) {
            hasLastCursor_ = false;
            return state;
        }
        auto& input = inputManager->GetQuery();

        // カーソル移動量は「操作可能かどうか」に関わらず毎フレーム更新する。
        // そうしないとドラッグ開始フレームに巨大な差分が入る。
        const POINT cursor = input.GetCursorPosition();
        const float cursorX = static_cast<float>(cursor.x);
        const float cursorY = static_cast<float>(cursor.y);
        if (hasLastCursor_) {
            state.mouseDelta = { cursorX - lastCursorX_, cursorY - lastCursorY_ };
        }
        lastCursorX_ = cursorX;
        lastCursorY_ = cursorY;
        hasLastCursor_ = true;

        state.active = IsCameraControlAllowed();
        if (!state.active) {
            state.mouseDelta = { 0.0f, 0.0f };
            return state;
        }

        const bool shift = input.IsKeyPressed(DIK_LSHIFT) || input.IsKeyPressed(DIK_RSHIFT);

        // ===== マウスボタン =====
        const bool middlePressed = input.IsMouseButtonPressed(MouseButton::Middle);
        state.orbitButton = middlePressed;
        state.panButton = middlePressed && shift;
        state.lookButton = input.IsMouseButtonPressed(MouseButton::Right);

        state.wheelDelta = input.GetWheelDelta();

        // 映像の大きさと、その中のカーソル位置（中央 0・右上 (1, 1)）
        if (viewportSize_.x > 0.0f && viewportSize_.y > 0.0f) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            state.viewportSize = viewportSize_;
            state.cursorNdc = {
                std::clamp((mouse.x - viewportOrigin_.x) / viewportSize_.x * 2.0f - 1.0f, -1.0f, 1.0f),
                std::clamp(1.0f - (mouse.y - viewportOrigin_.y) / viewportSize_.y * 2.0f, -1.0f, 1.0f)
            };
        }

        // ===== キーボード =====
        // テキスト入力中は ImGui へ譲る（マウス操作は従来どおり奪わない）
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            state.forward = input.IsKeyPressed(DIK_W);
            state.back = input.IsKeyPressed(DIK_S);
            state.right = input.IsKeyPressed(DIK_D);
            state.left = input.IsKeyPressed(DIK_A);
            state.up = input.IsKeyPressed(DIK_E) || input.IsKeyPressed(DIK_SPACE);
            state.down = input.IsKeyPressed(DIK_Q) || input.IsKeyPressed(DIK_LCONTROL);
            state.boost = shift;
        }

        return state;
    }

    void EditorCameraInput::SetViewportRect(const Vector2& origin, const Vector2& size)
    {
        viewportOrigin_ = origin;
        viewportSize_ = size;
    }
}

#endif // CORE_EDITOR
