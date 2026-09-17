#include "pch.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#ifdef USE_IMGUI

#include "SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/PlaybackState.h"
#include "Input/InputManager.h"
#include "Camera/CameraManager.h"
#include "Camera/CameraSceneStateIO.h"
#include "Editor/Camera/Module/CameraEditorContext.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/Inspector/InspectorRenderer.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Component/Core/ComponentFactory.h"
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
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/ImGui/Gizmo.h"
#include "Math/Geometry/RayCast.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
#include <filesystem>

namespace
{
    /// @brief トランスフォームとモデルファイルのメッシュ描画を持つ素のオブジェクトを作ってシーンへ登録する
    /// @return 登録できなければ nullptr
    CoreEngine::GameObject* CreateModelObject(CoreEngine::GameObjectManager& manager,
                                              const std::string& name, const std::string& modelPath)
    {
        auto owned = std::make_unique<CoreEngine::GameObject>();
        owned->SetName(name);
        CoreEngine::GameObject* object = manager.AddObject(std::move(owned));
        if (!object) {
            return nullptr;
        }
        // エディタが作るオブジェクトのコンポーネントとして付ける
        CoreEngine::ComponentHost::DataAttachScope dataScope(*object);
        object->AddComponent<CoreEngine::TransformComponent>();
        object->AddComponent<CoreEngine::MeshRendererComponent>(modelPath);
        return object;
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

        // 読み込んだ直後のシーンは保存済みとして扱う
        savedRevision_ = Editor::EditorCommandStack::Get().GetSceneRevision();

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
            if (!RefuseSaveWhilePlaying()) {
                saveSystem_->SaveObject(obj);
            }
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
        savedRevision_ = Editor::EditorCommandStack::Get().GetSceneRevision();
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
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
            SaveScene();
        }

        // Ctrl+C でも選択中のオブジェクトを複製する（Ctrl+D と同じ）
        if (!ImGui::GetIO().WantTextInput &&
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, ImGuiInputFlags_RouteGlobal)) {
            DuplicateSelectedObject();
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

        HandleSelectionShortcuts();

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

    bool SceneDebugEditor::SaveScene()
    {
        if (!saveSystem_ || saveSystem_->GetSceneName().empty()) {
            return false;
        }
        if (RefuseSaveWhilePlaying()) {
            return false;
        }

        saveSystem_->SaveScene(gameObjectManager_);

        // カメラの構図もシーンの一部として一緒に保存する。
        // これが無いと、エディタで詰めた画がアプリを閉じるたびに消える。
        if (cameraManager_) {
            CameraSceneStateIO::Save(saveSystem_->GetSceneName(), *cameraManager_);
        }

        savedRevision_ = Editor::EditorCommandStack::Get().GetSceneRevision();
        dirtyWithoutEdits_ = false;
        return true;
    }

    bool SceneDebugEditor::IsSceneDirty() const
    {
        return dirtyWithoutEdits_ || Editor::EditorCommandStack::Get().GetSceneRevision() != savedRevision_;
    }

    bool SceneDebugEditor::RefuseSaveWhilePlaying() const
    {
        if (!PlaybackStateManager::GetInstance().IsInPlayMode()) {
            return false;
        }

        constexpr const char* kMessage = "再生中は保存できません。停止してから保存してください";
        if (DockingUI* const dockingUI = engine_ ? engine_->GetDebugSubsystem()->GetDockingUI() : nullptr) {
            dockingUI->ShowStatusMessage(kMessage, Editor::Theme::kWarn);
        }
        Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System, "SceneDebugEditor: {}", kMessage);
        return true;
    }

    std::string SceneDebugEditor::GetSceneName() const
    {
        return saveSystem_ ? saveSystem_->GetSceneName() : std::string{};
    }

    void SceneDebugEditor::DrawHierarchyContent()
    {
        if (auto child = UI::Scope::ChildScope("##HierarchyObjectList")) {
            // シーンの名前の枝（保存していない変更があれば * を付ける）
            std::string sceneLabel = GetSceneName();
            if (sceneLabel.empty()) {
                sceneLabel = "Scene";
            }
            if (IsSceneDirty()) {
                sceneLabel += " *";
            }
            if (ImGui::TreeNodeEx("##sceneRoot", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
                    "%s", sceneLabel.c_str())) {
                for (const auto& obj : gameObjectManager_->GetAllObjects()) {
                    if (obj) {
                        DrawHierarchyRow(*obj);
                    }
                }
                ImGui::TreePop();
            }
        }
    }

    void SceneDebugEditor::DrawHierarchyRow(GameObject& object)
    {
        namespace Theme = Editor::Theme;

        const bool isSelected = objectSelector_.GetSelectedObject() == &object;
        const bool isPrefab = object.IsPrefabInstance();
        const ComponentFactory& factory = ComponentFactory::Get();
        const bool hasScript = std::any_of(object.GetAllComponents().begin(), object.GetAllComponents().end(),
            [&factory](const auto& component) {
                return component && factory.IsRuntimeType(component->GetTypeName());
            });

        ImGui::PushID(&object);
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const float rowHeight = ImGui::GetTextLineHeight();
        if (ImGui::Selectable("##row", isSelected, ImGuiSelectableFlags_SpanAvailWidth, ImVec2(0.0f, rowHeight))) {
            objectSelector_.SelectObject(&object);
        }
        const float rowRight = ImGui::GetItemRectMax().x;

        // インスペクタの ObjectRef 欄へ落とせるように ID を運ぶ
        if (ImGui::BeginDragDropSource()) {
            const std::uint64_t idValue = object.GetObjectId().value;
            ImGui::SetDragDropPayload(InspectorRenderer::kObjectDragPayload, &idValue, sizeof(idValue));
            ImGui::TextUnformatted(object.GetDisplayName());
            ImGui::EndDragDropSource();
        }
        DrawObjectContextMenu(object);

        // 状態に応じた色（破棄待ちは赤、非アクティブは淡く）
        ImVec4 textColor = Theme::kText;
        ImVec4 glyphColor = isPrefab ? Theme::kAccentHover : Theme::kTextDim;
        if (object.IsMarkedForDestroy()) {
            textColor = glyphColor = Theme::kError;
        } else if (!object.IsActive()) {
            textColor = glyphColor = Theme::kTextMute;
        }

        // 種類の記号（プレハブから作ったものは ◈）
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        const char* const glyph = isPrefab ? "◈" : "◆";
        drawList->AddText(rowMin, ImGui::GetColorU32(glyphColor), glyph);
        const float left = rowMin.x + ImGui::CalcTextSize(glyph).x + 6.0f;

        // 右端の札（名前の場所が無くなるほど狭いときは出さない）
        float right = rowRight - 4.0f;
        const auto placeTag = [&](const char* text, const ImVec4& color) {
            const ImVec2 size = UI::Bar::TagSize(text);
            if (size.x > (right - left) * 0.5f) {
                return;
            }
            right -= size.x;
            UI::Bar::DrawTag(drawList, ImVec2(right, rowMin.y + (rowHeight - size.y) * 0.5f), text, color);
            right -= 4.0f;
        };
        if (isPrefab) {
            placeTag("Prefab", Theme::kAccentHover);
        }
        if (hasScript) {
            placeTag("AS", Theme::kScript);
        }

        // 名前（入りきらなければ省略記号で詰める）
        UI::Bar::EllipsizedText(drawList, ImVec2(left, rowMin.y), ImVec2(right, rowMin.y + rowHeight),
            object.GetDisplayName(), textColor);
        ImGui::PopID();
    }

    void SceneDebugEditor::DrawInspectorContent()
    {
        GameObject* selected = objectSelector_.GetSelectedObject();
        GameObject* selectedSprite = objectSelector_.GetSelectedSprite();

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

    ObjectEditing::Context SceneDebugEditor::MakeObjectEditingContext()
    {
        ObjectEditing::Context context;
        context.manager = gameObjectManager_;
        context.beforeDestroy = [this](const GameObject& object) {
            if (objectSelector_.GetSelectedObject() == &object || objectSelector_.GetSelectedSprite() == &object) {
                objectSelector_.ClearSelection();
            }
        };
        return context;
    }

    void SceneDebugEditor::CreateEmptyObject()
    {
        if (!gameObjectManager_) {
            return;
        }
        if (GameObject* const created = ObjectEditing::CreateEmpty(MakeObjectEditingContext(), ComputeDropPosition(nullptr))) {
            objectSelector_.SelectObject(created);
        }
    }

    void SceneDebugEditor::CreateParticleObject(ObjectEditing::ParticleKind kind)
    {
        if (!gameObjectManager_) {
            return;
        }
        if (GameObject* const created = ObjectEditing::CreateParticle(
                MakeObjectEditingContext(), kind, ComputeDropPosition(nullptr))) {
            objectSelector_.SelectObject(created);
        }
    }

    void SceneDebugEditor::CreateUIObject(ObjectEditing::UIElementKind kind)
    {
        if (!gameObjectManager_) {
            return;
        }
        if (GameObject* const created = ObjectEditing::CreateUI(MakeObjectEditingContext(), kind)) {
            objectSelector_.SelectObject(created);
        }
    }

    bool SceneDebugEditor::ApplyToPrefab(GameObject& object)
    {
        if (!gameObjectManager_ || !object.IsPrefabInstance()) {
            return false;
        }
        if (!PrefabEditing::ApplyObject(*gameObjectManager_, object)) {
            return false;
        }
        ShowSaveNotification("プレハブへ適用しました: " + object.GetPrefab().GetPath());
        return true;
    }

    bool SceneDebugEditor::CanEditSelectedObject(std::string* reason) const
    {
        const GameObject* const selected = objectSelector_.GetSelectedObject();
        if (!selected) {
            if (reason) {
                *reason = "オブジェクトを選んでいません";
            }
            return false;
        }
        return ObjectEditing::CanDuplicateOrDelete(*selected, reason);
    }

    bool SceneDebugEditor::DuplicateSelectedObject()
    {
        const GameObject* const selected = objectSelector_.GetSelectedObject();
        if (!gameObjectManager_ || !selected) {
            return false;
        }
        GameObject* const copy = ObjectEditing::Duplicate(MakeObjectEditingContext(), *selected);
        if (!copy) {
            return false;
        }
        objectSelector_.SelectObject(copy);
        return true;
    }

    bool SceneDebugEditor::DeleteSelectedObject()
    {
        GameObject* const selected = objectSelector_.GetSelectedObject();
        if (!gameObjectManager_ || !selected) {
            return false;
        }
        return ObjectEditing::Delete(MakeObjectEditingContext(), *selected);
    }

    void SceneDebugEditor::HandleSelectionShortcuts()
    {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D)) {
            DuplicateSelectedObject();
        }
        if (ImGui::Shortcut(ImGuiKey_Delete)) {
            DeleteSelectedObject();
        }
    }

    void SceneDebugEditor::SpawnModelFromFile(const std::string& modelFileName, const Vector2* normalizedDropPos)
    {
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System, "モデルをスポーン: {}", modelFileName);

        // ファイル名から拡張子を除いたものを名前にする
        std::filesystem::path p(modelFileName);
        std::string name = p.stem().string();

        GameObject* raw = CreateModelObject(*gameObjectManager_, name, modelFileName);
        if (!raw) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System, "モデルのスポーンに失敗しました: {}", modelFileName);
            return;
        }

        // 動的スポーン時は PBR テクスチャを活かしつつ、問題のある法線マップのみ無効化する
        if (auto* mesh = raw->GetComponent<MeshRendererComponent>()) {
            ApplyDynamicModelMaterialOverrides(mesh->GetModel());
        }

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
                ApplyToPrefab(object);
            }
            if (ImGui::MenuItem("プレハブとのつながりを外す")) {
                PrefabEditing::Unlink(object);
            }
        } else {
            if (ImGui::MenuItem("プレハブとして保存")) {
                if (const AssetInfo* info = PrefabEditing::CreateFromObject(object)) {
                    ShowSaveNotification("プレハブを作りました: " + ToAssetPath(*info));
                }
            }
        }
        ImGui::EndPopup();
    }
}

#endif // USE_IMGUI
