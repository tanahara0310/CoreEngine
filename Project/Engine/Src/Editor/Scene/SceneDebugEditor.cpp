#include "pch.h"
#ifdef USE_IMGUI

#include "SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "Camera/CameraManager.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Model/DynamicModelObject.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "Scene/SceneSaveSystem.h"
#include "Editor/ImGui/ObjectSelector.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Gizmo.h"
#include "Graphics/Texture/TextureManager.h"
#include "GameObject/Sprite/SpriteObject.h"
#include "Math/Geometry/RayCast.h"
#include "Utility/Logger/Logger.h"
#include <cctype>
#include <filesystem>

namespace
{
    /// @brief 名前が " (n)" のコピー接尾辞で終わるか調べ、基底名を返す
    bool EndsWithUnityCopySuffix(const std::string& name, std::string* outBaseName)
    {
        if (name.size() < 4 || name.back() != ')') {
            return false;
        }

        const size_t openParen = name.rfind(" (");
        if (openParen == std::string::npos || openParen + 3 >= name.size()) {
            return false;
        }

        for (size_t i = openParen + 2; i + 1 < name.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
                return false;
            }
        }

        if (outBaseName) {
            *outBaseName = name.substr(0, openParen);
        }
        return true;
    }

    /// @brief 同名のオブジェクトが既に登録されているか
    bool HasObjectName(const CoreEngine::GameObjectManager* manager, const std::string& name)
    {
        if (!manager) {
            return false;
        }

        for (const auto& obj : manager->GetAllObjects()) {
            if (obj && obj->GetName() == name) {
                return true;
            }
        }

        return false;
    }

    /// @brief Unity 風の重複しないコピー名（"Name (1)"）を作る
    std::string GenerateUnityStyleCopyName(const CoreEngine::GameObjectManager* manager, const std::string& sourceName)
    {
        std::string baseName = sourceName;
        EndsWithUnityCopySuffix(sourceName, &baseName);

        for (int copyIndex = 1;; ++copyIndex) {
            const std::string candidate = baseName + " (" + std::to_string(copyIndex) + ")";
            if (!HasObjectName(manager, candidate)) {
                return candidate;
            }
        }
    }

    /// @brief 動的生成モデルへ既定のマテリアル上書きを適用する
    void ApplyDynamicModelMaterialOverrides(CoreEngine::Model* model)
    {
        if (!model) {
            return;
        }

        model->ForEachMaterial([](CoreEngine::MaterialInstance* material) {
            material->SetLightingEnabled(true);
            // PBR ファクター（metallic/roughness/color 等）は Model::Initialize() が
            // アセット側の値を適用済みのため、ここでは上書きしない。
            // IBL はシーン側で有効化されるため個別設定は不要。
            material->SetNormalMapEnabled(false);
        });
    }
}

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
                if (auto* src = obj->GetComponent<ITransformSource>()) {
                    record.translateAfter = src->Translate();
                    record.rotateAfter = src->Rotate();
                    record.scaleAfter = src->Scale();
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
                if (auto* src = obj->GetComponent<ITransformSource>()) {
                    record.translateAfter = src->Translate();
                    record.rotateAfter = src->Rotate();
                    record.scaleAfter = src->Scale();
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

    void SceneDebugEditor::DetachFromEngineUI()
    {
        if (!engine_) {
            return;
        }

        auto* debug = engine_->GetDebugSubsystem();
        if (!debug) {
            return;
        }

        if (auto* gameDebugUI = debug->GetGameDebugUI()) {
            gameDebugUI->SetSceneDebugEditor(nullptr);
            gameDebugUI->SetHierarchyContentDrawer(nullptr);
            gameDebugUI->SetInspectorCameraDrawer(nullptr);
            gameDebugUI->SetInspectorObjectDrawer(nullptr);
        }
        if (auto* dockingUI = debug->GetDockingUI()) {
            dockingUI->SetSceneDebugEditor(nullptr);
        }
    }

    void SceneDebugEditor::ClearHistory()
    {
        undoRedoHistory_.Clear();
    }

    void SceneDebugEditor::Update()
    {
        // デバッグ / リリースカメラの切り替え
        if (auto* inputManager = engine_->GetService<InputManager>()) {
            auto& input = inputManager->GetQuery();
            // 「どちらの視点で覗くか」はフラグ 1 つ。以前は アクティブカメラ名 と
            // Gameビュー上書き名 の 2 状態を両方更新する必要があり、片方だけ変える UI が
            // あったせいで描画とギズモが別カメラを見る状態が起きていた。
            if (input.IsKeyTriggered(DIK_1)) {
                cameraManager_->SetUseSceneCamera(true);
            } else if (input.IsKeyTriggered(DIK_2)) {
                cameraManager_->SetUseSceneCamera(false);
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

        const Camera* camera3D = cameraManager_ ? cameraManager_->GetActiveCamera(CameraType::Camera3D) : nullptr;
        const Camera* camera2D = cameraManager_ ? cameraManager_->GetActiveCamera(CameraType::Camera2D) : nullptr;

        if (camera3D) {
            objectSelector_.Update(gameObjectManager_, camera3D, normalizedMousePos, isViewportHovered);
            objectSelector_.DrawGizmo(camera3D);
        }

        if (camera2D) {
            objectSelector_.Update2D(gameObjectManager_, camera2D, normalizedMousePos, isViewportHovered);
            objectSelector_.DrawGizmo2D(camera2D);
        }
    }

    bool SceneDebugEditor::AcceptGameViewportModelDrop(const ImVec2& viewportPos, const ImVec2& viewportSize)
    {
        if (!gameObjectManager_) {
            return false;
        }

        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
            return false;
        }

        if (!ImGui::BeginDragDropTarget()) {
            return false;
        }

        bool accepted = false;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_FILE")) {
            const char* droppedFilename = static_cast<const char*>(payload->Data);
            if (droppedFilename && droppedFilename[0] != '\0') {
                const ImVec2 mousePos = ImGui::GetMousePos();
                const Vector2 normalizedDropPos(
                    std::clamp((mousePos.x - viewportPos.x) / viewportSize.x, 0.0f, 1.0f),
                    std::clamp((mousePos.y - viewportPos.y) / viewportSize.y, 0.0f, 1.0f));
                SpawnModelFromFile(droppedFilename, &normalizedDropPos);
                accepted = true;
            }
        }

        ImGui::EndDragDropTarget();
        return accepted;
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

        // メッシュを持つオブジェクトかどうか確認する（具象クラスではなくコンポーネントで判定）
        if (!selected->HasComponent<MeshRendererComponent>()) {
            Logger::GetInstance().Log("選択オブジェクトはメッシュを持たないためコピーできません", LogLevel::Warn, LogCategory::System);
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

        // 名前を設定（Unity 風の "Name (1)" 形式で一意化）
        std::string copyName = GenerateUnityStyleCopyName(gameObjectManager_, selected->GetName());
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

        raw->SetName(copyName);
        ApplyDynamicModelMaterialOverrides(raw->GetModel());

        // 少しオフセットを加えて重ならないようにする
        if (auto* src = raw->GetComponent<ITransformSource>()) {
            src->Translate().x += 1.0f;
        }

        // コピー操作を Undo 履歴に記録する
        ObjectSpawnRecord spawnRecord;
        spawnRecord.objectName = raw->GetName(); // GameObjectManager で確定した名前を使う
        spawnRecord.modelPath  = modelPath;
        if (auto* src = raw->GetComponent<ITransformSource>()) {
            spawnRecord.translate = src->Translate();
            spawnRecord.rotate    = src->Rotate();
            spawnRecord.scale     = src->Scale();
        }
        undoRedoHistory_.Push(spawnRecord);

        // コピーしたオブジェクトを選択状態にする
        objectSelector_.SelectObject(raw);

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System, "オブジェクトをコピーしました: {}", raw->GetName());
        return true;
    }

    void SceneDebugEditor::SpawnModelFromFile(const std::string& modelFileName, const Vector2* normalizedDropPos)
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

        // 動的スポーン時は PBR テクスチャを活かしつつ、問題のある法線マップのみ無効化する
        ApplyDynamicModelMaterialOverrides(raw->GetModel());

        if (auto* src = raw->GetComponent<ITransformSource>()) {
            Vector3 spawnPosition = { 0.0f, 1.0f, 0.0f };

            if (const Camera* camera3D = cameraManager_ ? cameraManager_->GetActiveCamera(CameraType::Camera3D) : nullptr) {
                const Vector2 dropPos = normalizedDropPos ? *normalizedDropPos : Vector2{ 0.5f, 0.5f };
                const Vector2 ndcPos(
                    dropPos.x * 2.0f - 1.0f,
                    1.0f - dropPos.y * 2.0f);

                const Vector3 nearPoint = MathCore::Coordinate::NormalizedScreenToWorld(
                    ndcPos,
                    0.0f,
                    camera3D->GetViewMatrix(),
                    camera3D->GetProjectionMatrix(),
                    1.0f,
                    1.0f);
                const Vector3 farPoint = MathCore::Coordinate::NormalizedScreenToWorld(
                    ndcPos,
                    1.0f,
                    camera3D->GetViewMatrix(),
                    camera3D->GetProjectionMatrix(),
                    1.0f,
                    1.0f);
                const Vector3 forward = CoreEngine::Normalize(farPoint - nearPoint);

                const Geometry::Ray ray{ camera3D->GetPosition(), forward };
                const Geometry::Plane groundPlane{ { 0.0f, 1.0f, 0.0f }, 0.0f };   // y = 0
                Geometry::RayHit hit{};
                if (Geometry::Raycast(ray, groundPlane, &hit)) {
                    spawnPosition = hit.point;
                } else {
                    spawnPosition = camera3D->GetPosition() + forward * 5.0f;
                    if (spawnPosition.y < 0.5f) {
                        spawnPosition.y = 0.5f;
                    }
                }
            }

            src->Translate() = spawnPosition;
        }

        // スポーンしたオブジェクトを選択状態にする
        objectSelector_.SelectObject(raw);

        // スポーン操作を Undo 履歴に記録する
        ObjectSpawnRecord spawnRecord;
        spawnRecord.objectName = raw->GetName();
        spawnRecord.modelPath  = modelFileName;
        if (auto* src = raw->GetComponent<ITransformSource>()) {
            spawnRecord.translate = src->Translate();
            spawnRecord.rotate    = src->Rotate();
            spawnRecord.scale     = src->Scale();
        }
        undoRedoHistory_.Push(spawnRecord);
    }
}

#endif // USE_IMGUI
