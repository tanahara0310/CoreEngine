#ifdef _DEBUG

#include "SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "Engine/Camera/CameraManager.h"
#include "Engine/Camera/Debug/CameraDebugUI.h"
#include "Engine/ObjectCommon/GameObjectManager.h"
#include "Engine/ObjectCommon/SpriteObject.h"
#include "Engine/Scene/SceneSaveSystem.h"
#include "Engine/Graphics/Line/LineManager.h"
#include "Engine/Utility/Debug/ImGui/SceneViewport.h"
#include "Engine/Utility/Debug/ImGui/ObjectSelector.h"
#include <imgui.h>

namespace CoreEngine
{
    void SceneDebugEditor::Initialize(EngineSystem* engine, GameObjectManager* mgr,
        CameraManager* camMgr, SceneSaveSystem* saveSystem)
    {
        engine_ = engine;
        gameObjectManager_ = mgr;
        cameraManager_ = camMgr;
        saveSystem_ = saveSystem;

        // 保存通知コールバックを設定
        saveSystem_->SetSaveNotificationCallback([this](const std::string& msg) {
            ShowSaveNotification(msg);
            });

        // 個別オブジェクト保存コールバック
        mgr->SetOnSaveRequestCallback([this](GameObject* obj) {
            saveSystem_->SaveSingle(obj);
            });

        // ギズモ変更時コールバックを設定
        auto imGuiManager = engine->GetImGuiManager();
        if (imGuiManager) {
            auto* sceneViewport = imGuiManager->GetSceneViewport();
            if (sceneViewport) {
                auto* objectSelector = sceneViewport->GetObjectSelector();
                if (objectSelector) {
                    // Undo/Redo 記録（ギズモ操作完了時）
                    objectSelector->SetOnGizmoEditCommitted([this](
                        GameObject* obj,
                        const Vector3& tBefore, const Vector3& rBefore,
                        const Vector3& sBefore, bool aBefore) {
                            if (!obj) return;
                            TransformRecord record;
                            record.objectName = obj->GetName();
                            record.translateBefore = tBefore;
                            record.rotateBefore = rBefore;
                            record.scaleBefore = sBefore;
                            record.activeBefore = aBefore;
                            auto* spriteObj = dynamic_cast<SpriteObject*>(obj);
                            if (spriteObj) {
                                record.translateAfter = spriteObj->GetSpriteTransform().translate;
                                record.rotateAfter = spriteObj->GetSpriteTransform().rotate;
                                record.scaleAfter = spriteObj->GetSpriteTransform().scale;
                            } else {
                                record.translateAfter = obj->GetTransform().translate;
                                record.rotateAfter = obj->GetTransform().rotate;
                                record.scaleAfter = obj->GetTransform().scale;
                            }
                            record.activeAfter = obj->IsActive();
                            undoRedoHistory_.Push(record);
                        });
                }
            }
        }

        // Undo/Redo 記録（ImGui 操作完了時）
        mgr->SetEditCommitCallback([this](
            GameObject* obj,
            const Vector3& tBefore, const Vector3& rBefore,
            const Vector3& sBefore, bool aBefore) {
                if (!obj) return;
                TransformRecord record;
                record.objectName = obj->GetName();
                record.translateBefore = tBefore;
                record.rotateBefore = rBefore;
                record.scaleBefore = sBefore;
                record.activeBefore = aBefore;
                auto* spriteObj = dynamic_cast<SpriteObject*>(obj);
                if (spriteObj) {
                    record.translateAfter = spriteObj->GetSpriteTransform().translate;
                    record.rotateAfter = spriteObj->GetSpriteTransform().rotate;
                    record.scaleAfter = spriteObj->GetSpriteTransform().scale;
                } else {
                    record.translateAfter = obj->GetTransform().translate;
                    record.rotateAfter = obj->GetTransform().rotate;
                    record.scaleAfter = obj->GetTransform().scale;
                }
                record.activeAfter = obj->IsActive();
                undoRedoHistory_.Push(record);
            });
    }

    void SceneDebugEditor::ClearHistory()
    {
        undoRedoHistory_.Clear();
    }

    void SceneDebugEditor::Update()
    {
        // デバッグカメラへの切り替え
        auto keyboard = engine_->GetComponent<KeyboardInput>();
        if (keyboard) {
            if (keyboard->IsKeyTriggered(DIK_F1)) {
                cameraManager_->SetActiveCamera("Debug", CameraType::Camera3D);
            } else if (keyboard->IsKeyTriggered(DIK_F2)) {
                cameraManager_->SetActiveCamera("Release", CameraType::Camera3D);
            }
        }

        // カメラマネージャーのImGui
        if (cameraManager_) {
            cameraManager_->DrawImGui();
        }

        // LineManagerのImGui
        LineManager::GetInstance().DrawImGui();

        // Ctrl+Z / Ctrl+Y によるキーボードショートカット（ウィンドウ外でも反応）
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
            undoRedoHistory_.Undo(gameObjectManager_);
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) {
            undoRedoHistory_.Redo(gameObjectManager_);
        }

        // Ctrl+S でシーン全体保存
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
            if (!saveSystem_->GetSceneName().empty()) {
                saveSystem_->Save(gameObjectManager_);
            }
        }

        // ゲームオブジェクトのImGuiデバッグUI表示
        DrawObjectControlWindow();

        // シーンビューポートでのオブジェクト選択とギズモ更新
        auto imGuiManager = engine_->GetImGuiManager();
        ICamera* activeCamera3D = cameraManager_->GetActiveCamera(CameraType::Camera3D);
        ICamera* activeCamera2D = cameraManager_->GetActiveCamera(CameraType::Camera2D);
        if (imGuiManager) {
            auto sceneViewport = imGuiManager->GetSceneViewport();
            if (sceneViewport) {
                if (activeCamera3D) {
                    sceneViewport->SetCamera(activeCamera3D);
                    sceneViewport->UpdateObjectSelection(gameObjectManager_, activeCamera3D);
                }
                if (activeCamera2D) {
                    sceneViewport->SetCamera2D(activeCamera2D);
                    sceneViewport->UpdateSpriteSelection(gameObjectManager_, activeCamera2D);
                }
            }
        }

        // 保存通知オーバーレイの描画
        DrawSaveNotification();
    }

    void SceneDebugEditor::DrawObjectControlWindow()
    {
        if (ImGui::Begin("オブジェクト制御")) {
            ImGui::BeginDisabled(saveSystem_->GetSceneName().empty());
            if (ImGui::Button("シーン全体保存 (Ctrl+S)")) {
                saveSystem_->Save(gameObjectManager_);
            }
            ImGui::EndDisabled();
            ImGui::Separator();

            ImGui::BeginDisabled(!undoRedoHistory_.CanUndo());
            if (ImGui::Button("Undo")) {
                undoRedoHistory_.Undo(gameObjectManager_);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!undoRedoHistory_.CanRedo());
            if (ImGui::Button("Redo")) {
                undoRedoHistory_.Redo(gameObjectManager_);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text("(%d / %d)",
                undoRedoHistory_.GetUndoCount(),
                undoRedoHistory_.GetUndoCount() + undoRedoHistory_.GetRedoCount());
            ImGui::Separator();

            gameObjectManager_->DrawAllImGui();
        }
        ImGui::End();
    }

    void SceneDebugEditor::ShowSaveNotification(const std::string& message)
    {
        saveNotificationMessage_ = message;
        saveNotificationEndTime_ = ImGui::GetTime() + kNotificationDuration;
    }

    void SceneDebugEditor::DrawSaveNotification()
    {
        double currentTime = ImGui::GetTime();
        if (currentTime >= saveNotificationEndTime_) return;

        // 残り時間からアルファ値を計算（最後の0.5秒でフェードアウト）
        double remaining = saveNotificationEndTime_ - currentTime;
        float alpha = (remaining < 0.5) ? static_cast<float>(remaining / 0.5) : 1.0f;

        // 画面中央上部に表示
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 windowPos = ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
            viewport->WorkPos.y + 20.0f
        );

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.75f * alpha);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.55f, 0.15f, 1.0f));

        if (ImGui::Begin("##SaveNotification", nullptr, flags)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, alpha));
            ImGui::Text("%s", saveNotificationMessage_.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
}

#endif // _DEBUG
