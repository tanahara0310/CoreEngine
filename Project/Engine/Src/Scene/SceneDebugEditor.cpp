#ifdef _DEBUG

#include "SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "Camera/CameraManager.h"
#include "ObjectCommon/GameObjectManager.h"
#include "Utility/Debug/ImGui/GameObjectDebugAccess.h"
#include "Scene/SceneSaveSystem.h"
#include "Utility/Debug/ImGui/SceneViewport.h"
#include "Utility/Debug/ImGui/ObjectSelector.h"
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

        // カメラエディター側で追従対象を参照できるよう、オブジェクトマネージャーを注入する。
        if (cameraManager_) {
            cameraManager_->SetDebugGameObjectManager(gameObjectManager_);
        }

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
                            DebugAccess::TransformAccess access;
                            if (DebugAccess::TryGetTransformAccess(obj, access)) {
                                record.translateAfter = *access.translate;
                                record.rotateAfter = *access.rotate;
                                record.scaleAfter = *access.scale;
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
                DebugAccess::TransformAccess access;
                if (DebugAccess::TryGetTransformAccess(obj, access)) {
                    record.translateAfter = *access.translate;
                    record.rotateAfter = *access.rotate;
                    record.scaleAfter = *access.scale;
                }
                record.activeAfter = obj->IsActive();
                undoRedoHistory_.Push(record);
            });

        // Hierarchy/Inspectorパネル用の描画コールバックをGameDebugUIに登録
        if (auto* gameDebugUI = engine_->GetGameDebugUI()) {
            gameDebugUI->SetHierarchyContentDrawer([this]() {
                DrawHierarchyContent();
            });
            gameDebugUI->SetInspectorCameraDrawer([this]() {
                if (cameraManager_) {
                    cameraManager_->DrawImGuiContent();
                }
            });
            gameDebugUI->SetInspectorObjectDrawer([this]() {
                DrawInspectorContent();
            });
        }
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

        // カメラデバッグモジュールの状態更新（描画はInspectorパネルで行う）
        if (cameraManager_) {
            cameraManager_->UpdateDebugModules();
        }

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

        // シーンビューポートでのオブジェクト選択とギズモ更新
        auto imGuiManager = engine_->GetImGuiManager();
        ICamera* activeCamera3D = cameraManager_->GetActiveCamera(CameraType::Camera3D);
        ICamera* activeCamera2D = cameraManager_->GetActiveCamera(CameraType::Camera2D);
        if (imGuiManager) {
            auto sceneViewport = imGuiManager->GetSceneViewport();
            if (sceneViewport) {
                if (activeCamera3D) {
                    sceneViewport->SetCamera(activeCamera3D);
                }
                if (activeCamera2D) {
                    sceneViewport->SetCamera2D(activeCamera2D);
                }
            }
        }

        // 保存通知オーバーレイの描画
        DrawSaveNotification();
    }

    void SceneDebugEditor::DrawHierarchyContent()
    {
        // ツールバー：保存 / Undo / Redo
        ImGui::BeginDisabled(saveSystem_->GetSceneName().empty());
        if (ImGui::Button("Save")) {
            saveSystem_->Save(gameObjectManager_);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
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
        ImGui::TextDisabled("(%d/%d)",
            undoRedoHistory_.GetUndoCount(),
            undoRedoHistory_.GetUndoCount() + undoRedoHistory_.GetRedoCount());
        ImGui::Separator();

        // ObjectSelectorを取得（クリック選択用）
        ObjectSelector* objectSelector = nullptr;
        if (auto* imGuiManager = engine_->GetImGuiManager()) {
            if (auto* sceneViewport = imGuiManager->GetSceneViewport()) {
                objectSelector = sceneViewport->GetObjectSelector();
            }
        }

        const auto& objects = gameObjectManager_->GetAllObjects();
        ImGui::TextDisabled("Objects: %zu", objects.size());
        ImGui::Separator();

        if (ImGui::BeginChild("##HierarchyObjectList", ImVec2(0.0f, 0.0f), false)) {
            for (const auto& obj : objects) {
                if (!obj) continue;

                const bool isSelected = objectSelector &&
                    (objectSelector->GetSelectedObject() == obj.get());

                // 状態に応じた文字色
                int colorsPushed = 0;
                if (obj->IsMarkedForDestroy()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    ++colorsPushed;
                } else if (!obj->IsActive()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                    ++colorsPushed;
                }

                const std::string& name = obj->GetName();
                const char* displayName = name.empty() ? obj->GetObjectName() : name.c_str();

                char itemId[256];
                snprintf(itemId, sizeof(itemId), "%s##obj_%p", displayName, (void*)obj.get());

                if (ImGui::Selectable(itemId, isSelected)) {
                    if (objectSelector) {
                        objectSelector->SelectObject(obj.get());
                    }
                }

                if (colorsPushed > 0) {
                    ImGui::PopStyleColor(colorsPushed);
                }
            }
        }
        ImGui::EndChild();
    }

    void SceneDebugEditor::DrawInspectorContent()
    {
        // ObjectSelectorから選択オブジェクトを取得
        ObjectSelector* objectSelector = nullptr;
        if (auto* imGuiManager = engine_->GetImGuiManager()) {
            if (auto* sceneViewport = imGuiManager->GetSceneViewport()) {
                objectSelector = sceneViewport->GetObjectSelector();
            }
        }

        if (!objectSelector) {
            ImGui::TextDisabled("初期化中...");
            return;
        }

        GameObject* selected = objectSelector->GetSelectedObject();
        gameObjectManager_->DrawSingleObjectImGui(selected);
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
