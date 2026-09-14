#include "pch.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#ifdef USE_IMGUI

#include "SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "Input/InputManager.h"
#include "Camera/CameraManager.h"
#include "Camera/CameraSceneStateIO.h"
#include "Editor/Camera/Module/CameraEditorContext.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/Inspector/InspectorRenderer.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Model/DynamicModelObject.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "Scene/PrefabSystem.h"
#include "Scene/SceneSaveSystem.h"
#include "Editor/Command/EditorCommand.h"
#include "Editor/Scene/PrefabEditing.h"
#include "Editor/ImGui/ObjectSelector.h"
#include "Graphics/Asset/AssetInfo.h"
#include "Graphics/Asset/AssetRef.h"
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

        undoRedoHistory_.SetGameObjectManager(mgr);

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

        // Hierarchy / Inspector の中身とカメラエディタをパネルとして登録する
        if (auto* gameDebugUI = engine_->GetDebugSubsystem()->GetGameDebugUI()) {
            gameDebugUI->SetSceneDebugEditor(this);
        }
        if (auto* dockingUI = engine_->GetDebugSubsystem()->GetDockingUI()) {
            dockingUI->SetSceneDebugEditor(this);
        }

        auto& panels = Editor::EditorPanelRegistry::Get();
        panels.Register({
            .id = "Hierarchy Content",
            .placement = Editor::PanelPlacement::HierarchyContent,
            .owner = this,
            .draw = [this]() { DrawHierarchyContent(); },
            });
        panels.Register({
            .id = "Inspector Object",
            .placement = Editor::PanelPlacement::InspectorObject,
            .owner = this,
            .draw = [this]() { DrawInspectorContent(); },
            });
        // Camera Editor は単独ウィンドウ。エディタ視点カメラの設定なので Editor グループへ
        panels.Register({
            .id = "Camera Editor",
            .placement = Editor::PanelPlacement::Window,
            .group = Editor::PanelGroup::Editor,
            .owner = this,
            .draw = [this]() {
                if (cameraManager_) {
                    cameraManager_->DrawImGuiContent();
                }
            },
            });
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
        }
        if (auto* dockingUI = debug->GetDockingUI()) {
            dockingUI->SetSceneDebugEditor(nullptr);
        }

        // 解放済みの this を描かないよう、自分が登録したパネルを外す
        auto& panels = Editor::EditorPanelRegistry::Get();
        panels.Unregister("Hierarchy Content", this);
        panels.Unregister("Inspector Object", this);
        panels.Unregister("Camera Editor", this);
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

        // Ctrl+Z / Ctrl+Y はここが唯一の受け口。オブジェクト・CVar・カメラ・ステージの
        // 操作はすべて EditorCommandStack の 1 本に積まれている。
        // テキスト入力中は ImGui 自身の入力 Undo に譲る。
        if (!ImGui::GetIO().WantTextInput) {
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
                undoRedoHistory_.Undo(gameObjectManager_);
            }
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal)) {
                undoRedoHistory_.Redo(gameObjectManager_);
            }
        }

        // Ctrl+S でシーン全体保存
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
            if (!saveSystem_->GetSceneName().empty()) {
                saveSystem_->SaveScene(gameObjectManager_);

                // カメラの構図もシーンの一部として一緒に保存する。
                // これが無いと、エディタで詰めた画がアプリを閉じるたびに消える。
                if (cameraManager_) {
                    CameraSceneStateIO::Save(saveSystem_->GetSceneName(), *cameraManager_);
                }
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

            // カメラ編集の重ね描き（キーのアイコン・ギズモ）はオブジェクト選択の後。
            // 同じフレームで両方が掴めると、どちらが動いたのか分からなくなる。
            if (cameraManager_ && !Gizmo::IsUsing()) {
                CameraEditorViewport cameraViewport{};
                cameraViewport.x = viewportPos.x;
                cameraViewport.y = viewportPos.y;
                cameraViewport.width = viewportSize.x;
                cameraViewport.height = viewportSize.y;
                cameraManager_->DrawDebugViewportOverlay(*camera3D, cameraViewport);
            }
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

        const ImVec2 mousePos = ImGui::GetMousePos();
        const Vector2 normalizedDropPos(
            std::clamp((mousePos.x - viewportPos.x) / viewportSize.x, 0.0f, 1.0f),
            std::clamp((mousePos.y - viewportPos.y) / viewportSize.y, 0.0f, 1.0f));

        bool accepted = false;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_FILE")) {
            const char* droppedFilename = static_cast<const char*>(payload->Data);
            if (droppedFilename && droppedFilename[0] != '\0') {
                SpawnModelFromFile(droppedFilename, &normalizedDropPos);
                accepted = true;
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PREFAB_FILE")) {
            const char* droppedFilename = static_cast<const char*>(payload->Data);
            if (droppedFilename && droppedFilename[0] != '\0') {
                SpawnPrefabFromFile(droppedFilename, &normalizedDropPos);
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
        // 履歴はエディタ共通の 1 本。次に何が戻るのかをボタンから読めるようにする
        auto& commandStack = Editor::EditorCommandStack::Get();
        ImGui::BeginDisabled(!undoRedoHistory_.CanUndo());
        if (ImGui::Button("Undo")) {
            undoRedoHistory_.Undo(gameObjectManager_);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && commandStack.CanUndo()) {
            ImGui::SetTooltip("戻す: %s", commandStack.PeekUndoLabel().c_str());
        }
        UI::SameLine();
        {
            UI::Scope::DisabledScope ds(!undoRedoHistory_.CanRedo());
            if (ImGui::Button("Redo")) {
                undoRedoHistory_.Redo(gameObjectManager_);
            }
        }
        if (ImGui::IsItemHovered() && commandStack.CanRedo()) {
            ImGui::SetTooltip("やり直す: %s", commandStack.PeekRedoLabel().c_str());
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

                // アイコンを表示（プレハブから作ったオブジェクトは青）
                if (sObjIconLoaded) {
                    const ImVec4 iconTint = obj->IsPrefabInstance()
                        ? ImGui::GetStyleColorVec4(ImGuiCol_CheckMark)
                        : ImVec4(0.96f, 0.65f, 0.14f, 1.0f);
                    ImGui::ImageWithBg((ImTextureID)sObjIconHandle.ptr, ImVec2(14, 14),
                        ImVec2(0, 0), ImVec2(1, 1),
                        ImVec4(0, 0, 0, 0),
                        iconTint);
                    ImGui::SameLine(0.0f, 4.0f);
                }

                const char* displayName = obj->GetDisplayName();

                char itemId[256];
                snprintf(itemId, sizeof(itemId), "%s##obj_%p", displayName, (void*)obj.get());

                if (ImGui::Selectable(itemId, isSelected)) {
                    objectSelector_.SelectObject(obj.get());
                }

                // インスペクタの ObjectRef 欄へ落とせるように ID を運ぶ
                if (ImGui::BeginDragDropSource()) {
                    const std::uint64_t idValue = obj->GetObjectId().value;
                    ImGui::SetDragDropPayload(
                        InspectorRenderer::kObjectDragPayload, &idValue, sizeof(idValue));
                    ImGui::TextUnformatted(displayName);
                    ImGui::EndDragDropSource();
                }

                if (colorsPushed > 0) {
                    ImGui::PopStyleColor(colorsPushed);
                }

                DrawObjectContextMenu(*obj);
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
        json serializedData = selected->Serialize();
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
            raw->Deserialize(serializedData);
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
            src->Translate() = ComputeDropPosition(normalizedDropPos);
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

    void SceneDebugEditor::SpawnPrefabFromFile(const std::string& prefabFileName, const Vector2* normalizedDropPos)
    {
        const AssetInfo* info = FindAssetInfo(prefabFileName);
        if (!info || info->type != AssetType::Prefab) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "プレハブが見つかりません: {}", prefabFileName);
            return;
        }

        const Reflection::AssetRefValue prefab{ info->guid, ToAssetPath(*info) };
        GameObject* placed = PrefabSystem::Instantiate(*gameObjectManager_, prefab, info->name);
        if (!placed) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                "プレハブからオブジェクトを作れませんでした: {}", prefab.path);
            return;
        }

        if (auto* src = placed->GetComponent<ITransformSource>()) {
            src->Translate() = ComputeDropPosition(normalizedDropPos);
        }
        objectSelector_.SelectObject(placed);

        // 置いた操作を Undo 履歴に記録する（戻すと消し、やり直すと同じ ID と状態で置き直す）
        const ObjectId id = placed->GetObjectId();
        const std::string name = placed->GetName();
        const json state = placed->Serialize();
        Editor::EditorCommandStack::Get().Push(std::make_unique<Editor::FunctionCommand>(
            name + " の配置",
            [this, id] {
                if (GameObject* target = gameObjectManager_->FindObject(id)) {
                    if (objectSelector_.GetSelectedObject() == target) {
                        objectSelector_.SelectObject(nullptr);
                    }
                    target->Destroy();
                }
            },
            [this, prefab, name, state, id] {
                if (GameObject* target = PrefabSystem::Instantiate(*gameObjectManager_, prefab, name)) {
                    gameObjectManager_->AssignObjectId(*target, id);
                    target->Deserialize(state);
                }
            }));

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "プレハブを置きました: {}（{}）", name, prefab.path);
    }

    Vector3 SceneDebugEditor::ComputeDropPosition(const Vector2* normalizedDropPos) const
    {
        Vector3 spawnPosition = { 0.0f, 1.0f, 0.0f };

        const Camera* camera3D = cameraManager_ ? cameraManager_->GetActiveCamera(CameraType::Camera3D) : nullptr;
        if (!camera3D) {
            return spawnPosition;
        }

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
        return spawnPosition;
    }

    void SceneDebugEditor::DrawObjectContextMenu(GameObject& object)
    {
        if (!ImGui::BeginPopupContextItem()) {
            return;
        }

        if (object.IsPrefabInstance()) {
            ImGui::TextDisabled("%s", object.GetPrefab().GetPath().c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("プレハブへ適用")) {
                if (PrefabEditing::ApplyObject(*gameObjectManager_, object)) {
                    ShowSaveNotification("プレハブへ適用しました: " + object.GetPrefab().GetPath());
                }
            }
            if (ImGui::MenuItem("プレハブとのつながりを外す")) {
                PrefabEditing::Unlink(object);
            }
        } else {
            // 型名で作り直すオブジェクト（UIText など）はプレハブにできない
            const bool plainObject = object.GetSerializeTypeName() == nullptr;
            if (ImGui::MenuItem("プレハブとして保存", nullptr, false, plainObject)) {
                if (const AssetInfo* info = PrefabEditing::CreateFromObject(object)) {
                    ShowSaveNotification("プレハブを作りました: " + ToAssetPath(*info));
                }
            }
        }
        ImGui::EndPopup();
    }
}

#endif // USE_IMGUI
