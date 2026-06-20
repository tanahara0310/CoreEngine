#include "pch.h"
#ifdef USE_IMGUI

#include "SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "Camera/CameraManager.h"
#include "ObjectCommon/GameObjectManager.h"
#include "Utility/Debug/ImGui/GameObjectDebugAccess.h"
#include "Scene/SceneSaveSystem.h"
#include "Utility/Debug/ImGui/ObjectSelector.h"
#include "Utility/Debug/ImGui/ImGuiAll.h"
#include "Utility/Debug/ImGui/Gizmo.h"
#include "Graphics/Texture/TextureManager.h"
#include "ObjectCommon/Sprite/SpriteObject.h"
#include "ObjectCommon/Model/DynamicModelObject.h"
#include "Utility/Logger/Logger.h"
#include <filesystem>

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
            cameraManager_->SetEngineSystem(engine_);
        }

        objectSelector_.Initialize();

        // 保存通知コールバックを設定
        saveSystem_->SetSaveNotificationCallback([this](const std::string& msg) {
            ShowSaveNotification(msg);
            });

        // 個別オブジェクト保存コールバック
        mgr->SetOnSaveRequestCallback([this](GameObject* obj) {
            saveSystem_->SaveObject(obj);
            });

        // ギズモ変更時コールバックを設定
        objectSelector_.SetOnGizmoEditCommitted([this](
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

        // Undo でオブジェクトが削除される直前に ObjectSelector の選択を解除する。
        // 解除しないと削除済みオブジェクトへのダングリングポインタでクラッシュする。
        undoRedoHistory_.SetOnBeforeDestroyCallback([this](const std::string& objectName) {
            if (objectSelector_.GetSelectedObject() &&
                objectSelector_.GetSelectedObject()->GetName() == objectName) {
                objectSelector_.SelectObject(nullptr);
            }
        });

        // Hierarchy/Inspectorパネル用の描画コールバックをGameDebugUIに登録
        if (auto* gameDebugUI = engine_->GetDebugSubsystem()->GetGameDebugUI()) {
            gameDebugUI->SetSceneDebugEditor(this);
            if (auto* dockingUI = engine_->GetDebugSubsystem()->GetDockingUI()) {
                dockingUI->SetSceneDebugEditor(this);
            }
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
        // デバッグ / リリースカメラの切り替え
        if (auto* inputManager = engine_->GetComponent<InputManager>()) {
            auto& input = inputManager->GetQuery();
            if (input.IsKeyTriggered(DIK_1)) {
                cameraManager_->SetActiveCamera("Debug", CameraType::Camera3D);
                cameraManager_->SetGameViewCameraOverride("Debug");
            } else if (input.IsKeyTriggered(DIK_2)) {
                cameraManager_->SetActiveCamera("Release", CameraType::Camera3D);
                cameraManager_->SetGameViewCameraOverride("Release");
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
                saveSystem_->SaveScene(gameObjectManager_);
            }
        }

        // Ctrl+C で選択中オブジェクトをコピー（複製）
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            CopySelectedObject();
        }

        // 保存通知オーバーレイの描画
        DrawSaveNotification();
    }

    void SceneDebugEditor::UpdateGameViewportInteraction(
        const ImVec2& viewportPos,
        const ImVec2& viewportSize,
        bool isViewportHovered)
    {
        if (!gameObjectManager_) {
            return;
        }

        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
            return;
        }

        Gizmo::Prepare(viewportPos, viewportSize);
        ImGuizmo::SetDrawlist();

        const ImVec2 mousePos = ImGui::GetMousePos();
        const Vector2 normalizedMousePos(
            (mousePos.x - viewportPos.x) / viewportSize.x,
            (mousePos.y - viewportPos.y) / viewportSize.y);

        const ICamera* camera3D = cameraManager_ ? cameraManager_->GetActiveCamera(CameraType::Camera3D) : nullptr;
        const ICamera* camera2D = cameraManager_ ? cameraManager_->GetActiveCamera(CameraType::Camera2D) : nullptr;

        if (camera3D) {
            objectSelector_.Update(gameObjectManager_, camera3D, normalizedMousePos, isViewportHovered);
            objectSelector_.DrawGizmo(camera3D);
        }

        if (camera2D) {
            objectSelector_.Update2D(gameObjectManager_, camera2D, normalizedMousePos, isViewportHovered);
            objectSelector_.DrawGizmo2D(camera2D);
        }
    }

    Gizmo::Mode SceneDebugEditor::GetGizmoMode() const
    {
        return objectSelector_.GetGizmoMode();
    }

    void SceneDebugEditor::SetGizmoMode(Gizmo::Mode mode)
    {
        objectSelector_.SetGizmoMode(mode);
    }

    void SceneDebugEditor::DrawHierarchyContent()
    {
        // ツールバー：保存 / Undo / Redo
        ImGui::BeginDisabled(saveSystem_->GetSceneName().empty());
        if (ImGui::Button("Save Scene")) {
            saveSystem_->SaveScene(gameObjectManager_);
        }
        ImGui::EndDisabled();
        UI::SameLine();
        ImGui::BeginDisabled(!undoRedoHistory_.CanUndo());
        if (ImGui::Button("Undo")) {
            undoRedoHistory_.Undo(gameObjectManager_);
        }
        ImGui::EndDisabled();
        UI::SameLine();
        {
            UI::Scope::DisabledScope ds(!undoRedoHistory_.CanRedo());
            if (ImGui::Button("Redo")) {
                undoRedoHistory_.Redo(gameObjectManager_);
            }
        }
        UI::SameLine();
        UI::HintF("(%d/%d)",
            undoRedoHistory_.GetUndoCount(),
            undoRedoHistory_.GetUndoCount() + undoRedoHistory_.GetRedoCount());
        UI::Separator();

        const auto& objects = gameObjectManager_->GetAllObjects();
        UI::Separator();

        if (auto child = UI::Scope::ChildScope("##HierarchyObjectList")) {
            // ── オブジェクトアイコンの初回ロード ──
            static D3D12_GPU_DESCRIPTOR_HANDLE sObjIconHandle{};
            static bool sObjIconLoaded = false;
            if (!sObjIconLoaded && TextureManager::GetInstance().IsInitialized()) {
                sObjIconHandle = TextureManager::GetInstance().Load("obj.png").gpuHandle;
                sObjIconLoaded = true;
            }

            for (const auto& obj : objects) {
                if (!obj) continue;

                const bool isSelected = (objectSelector_.GetSelectedObject() == obj.get());

                // 状態に応じた文字色
                int colorsPushed = 0;
                if (obj->IsMarkedForDestroy()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    ++colorsPushed;
                } else if (!obj->IsActive()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                    ++colorsPushed;
                }

                // アイコンを表示
                if (sObjIconLoaded) {
                    ImGui::ImageWithBg((ImTextureID)sObjIconHandle.ptr, ImVec2(14, 14),
                        ImVec2(0, 0), ImVec2(1, 1),
                        ImVec4(0, 0, 0, 0),
                        ImVec4(0.96f, 0.65f, 0.14f, 1.0f));
                    ImGui::SameLine(0.0f, 4.0f);
                }

                const char* displayName = obj->GetDisplayName();

                char itemId[256];
                snprintf(itemId, sizeof(itemId), "%s##obj_%p", displayName, (void*)obj.get());

                if (ImGui::Selectable(itemId, isSelected)) {
                    objectSelector_.SelectObject(obj.get());
                }

                if (colorsPushed > 0) {
                    ImGui::PopStyleColor(colorsPushed);
                }
            }
        }
    }

    void SceneDebugEditor::DrawInspectorContent()
    {
        GameObject* selected = objectSelector_.GetSelectedObject();
        SpriteObject* selectedSprite = objectSelector_.GetSelectedSprite();

        if (selectedSprite) {
            gameObjectManager_->DrawSingleObjectImGui(selectedSprite);
        } else {
            gameObjectManager_->DrawSingleObjectImGui(selected);
        }
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

    bool SceneDebugEditor::CopySelectedObject()
    {
        // 選択中のオブジェクトを取得
        GameObject* selected = objectSelector_.GetSelectedObject();
        if (!selected) {
            Logger::GetInstance().Log("コピー対象のオブジェクトが選択されていません", LogLevel::Warn, LogCategory::System);
            return false;
        }

        // ModelGameObject 派生かどうか確認する（DynamicModel 以外の既存モデルも対象）
        auto* modelObj = dynamic_cast<ModelGameObject*>(selected);
        if (!modelObj) {
            Logger::GetInstance().Log("選択オブジェクトはModelGameObjectではないためコピーできません", LogLevel::Warn, LogCategory::System);
            return false;
        }

        // シリアライズデータからモデルパスを取得する
        json serializedData = selected->OnSerialize();
        std::string modelPath;
        if (serializedData.contains("modelPath")) {
            modelPath = serializedData["modelPath"].get<std::string>();
        }

        if (modelPath.empty()) {
            Logger::GetInstance().Log("モデルパスが取得できないためコピーできません", LogLevel::Warn, LogCategory::System);
            return false;
        }

        // DynamicModelObject として複製を生成
        auto newObj = std::make_unique<DynamicModelObject>();
        newObj->SetModelPath(modelPath);

        // 名前を設定（"_Copy" を付加）
        std::string copyName = std::string(selected->GetDisplayName()) + "_Copy";
        newObj->SetName(copyName);

        // 登録して Initialize
        DynamicModelObject* raw = gameObjectManager_->AddObject(std::move(newObj));
        if (!raw) {
            Logger::GetInstance().Log("オブジェクトのコピーに失敗しました", LogLevel::Error, LogCategory::System);
            return false;
        }

        // シリアライズデータを復元（トランスフォームを引き継ぐ）
        if (!serializedData.empty()) {
            raw->OnDeserialize(serializedData);
        }

        // 少しオフセットを加えて重ならないようにする
        DebugAccess::TransformAccess access;
        if (DebugAccess::TryGetTransformAccess(raw, access)) {
            if (access.translate) {
                access.translate->x += 1.0f;
            }
        }

        // コピー操作を Undo 履歴に記録する
        ObjectSpawnRecord spawnRecord;
        spawnRecord.objectName = raw->GetName(); // GameObjectManager で確定した名前を使う
        spawnRecord.modelPath  = modelPath;
        if (DebugAccess::TryGetTransformAccess(raw, access)) {
            spawnRecord.translate = *access.translate;
            spawnRecord.rotate    = *access.rotate;
            spawnRecord.scale     = *access.scale;
        }
        undoRedoHistory_.Push(spawnRecord);

        // コピーしたオブジェクトを選択状態にする
        objectSelector_.SelectObject(raw);

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System, "オブジェクトをコピーしました: {}", raw->GetName());
        return true;
    }

    void SceneDebugEditor::SpawnModelFromFile(const std::string& modelFileName)
    {
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System, "モデルをスポーン: {}", modelFileName);

        auto obj = std::make_unique<DynamicModelObject>();
        obj->SetModelPath(modelFileName);

        // ファイル名から拡張子を除いたものを名前にする
        std::filesystem::path p(modelFileName);
        std::string name = p.stem().string();
        obj->SetName(name);

        DynamicModelObject* raw = gameObjectManager_->AddObject(std::move(obj));
        if (!raw) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System, "モデルのスポーンに失敗しました: {}", modelFileName);
            return;
        }

        // スポーンしたオブジェクトを選択状態にする
        objectSelector_.SelectObject(raw);

        // スポーン操作を Undo 履歴に記録する
        ObjectSpawnRecord spawnRecord;
        spawnRecord.objectName = raw->GetName();
        spawnRecord.modelPath  = modelFileName;
        DebugAccess::TransformAccess access;
        if (DebugAccess::TryGetTransformAccess(raw, access)) {
            spawnRecord.translate = *access.translate;
            spawnRecord.rotate    = *access.rotate;
            spawnRecord.scale     = *access.scale;
        }
        undoRedoHistory_.Push(spawnRecord);
    }
}

#endif // USE_IMGUI
