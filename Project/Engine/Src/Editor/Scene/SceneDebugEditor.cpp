#include "pch.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#ifdef CORE_EDITOR

#include "SceneDebugEditor.h"
#include "Editor/Scene/SceneViewTools.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/PlaybackState.h"
#include "Input/InputManager.h"
#include "Camera/CameraManager.h"
#include "Camera/CameraSceneStateIO.h"
#include "Editor/Camera/Module/CameraEditorContext.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/Inspector/InspectorRenderer.h"
#include "Editor/Inspector/ObjectInspector.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "Scene/Scene.h"
#include "Scene/PrefabSystem.h"
#include "Scene/SceneManager.h"
#include "Scene/SceneSaveSystem.h"
#include "Editor/Command/EditorCommand.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Scene/LastOpenedScene.h"
#include "Editor/Scene/PrefabEditing.h"
#include "Editor/ImGui/ObjectSelector.h"
#include "Graphics/Asset/AssetDatabase.h"
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
    /// @brief 選んだ行へ送るのを続けるフレーム数
    /// @details 一覧の高さが確定するまで待つ。1 フレームだと上限で丸められて届かない。
    constexpr int kScrollFrames = 3;

    /// @brief トランスフォームとモデルファイルのメッシュ描画を持つ素のオブジェクトを作ってシーンへ登録する
    /// @return 登録できなければ nullptr
    CoreEngine::GameObject* CreateModelObject(CoreEngine::GameObjectManager& manager,
                                              const std::string& name, const std::string& modelPath)
    {
        auto owned = std::make_unique<CoreEngine::GameObject>();
        owned->SetSerializeKey(CoreEngine::ObjectEditing::MakeNewObjectKey(name));
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

        // 編集で開いたシーンを、次にエディタを起動したときに開くシーンとして控える
        if (!PlaybackStateManager::GetInstance().IsInPlayMode()) {
            Editor::LastOpenedScene::Save(saveSystem_->GetSceneName());
        }

        // カメラエディター側で追従対象を参照できるよう、オブジェクトマネージャーを注入する。
        if (cameraManager_) {
            cameraManager_->SetDebugGameObjectManager(gameObjectManager_);
            cameraManager_->SetEngineSystem(engine_);
        }

        objectSelector_.Initialize(gameObjectManager_);

        // 保存通知コールバックを設定
        saveSystem_->SetSaveNotificationCallback([this](const std::string& msg) {
            ShowSaveNotification(msg);
            });

        // ギズモで動かし終えたら、移動を Undo に積む
        objectSelector_.SetOnGizmoEditCommitted([this](
            GameObject* obj,
            const Vector3& tBefore, const Vector3& rBefore,
            const Vector3& sBefore, bool aBefore) {
                if (!obj) return;
                TransformRecord record;
                record.objectId = obj->GetObjectId();
                record.objectName = obj->GetName();
                record.translateBefore = tBefore;
                record.rotateBefore = rBefore;
                record.scaleBefore = sBefore;
                record.activeBefore = aBefore;
                if (auto* src = obj->GetComponent<ITransformSource>()) {
                    record.translateAfter = src->GetTranslate();
                    record.rotateAfter = src->GetRotate();
                    record.scaleAfter = src->GetScale();
                }
                record.activeAfter = obj->IsActive();
                undoRedoHistory_.Push(record);
            });

        // エンジン常駐の UI に結びつけ、Hierarchy / Inspector の中身とカメラエディタをパネルとして登録する
        if (auto* gameDebugUI = engine_->GetDebugSubsystem()->GetGameDebugUI()) {
            engineUIRegistrations_.push_back(gameDebugUI->BindSceneDebugEditor(*this));
        }
        if (auto* dockingUI = engine_->GetDebugSubsystem()->GetDockingUI()) {
            engineUIRegistrations_.push_back(dockingUI->BindSceneDebugEditor(*this));
        }

        auto& panels = Editor::EditorPanelRegistry::Get();
        engineUIRegistrations_.push_back(panels.Register({
            .id = "Hierarchy Content",
            .placement = Editor::PanelPlacement::HierarchyContent,
            .draw = [this]() { DrawHierarchyContent(); },
            }));
        engineUIRegistrations_.push_back(panels.Register({
            .id = "Inspector Object",
            .placement = Editor::PanelPlacement::InspectorObject,
            .draw = [this]() { DrawInspectorContent(); },
            }));
        // Camera Editor は単独ウィンドウ。エディタ視点カメラの設定なので Editor グループへ
        engineUIRegistrations_.push_back(panels.Register({
            .id = "Camera Editor",
            .placement = Editor::PanelPlacement::Window,
            .group = Editor::PanelGroup::Editor,
            .draw = [this]() {
                if (cameraManager_) {
                    cameraManager_->DrawImGuiContent();
                }
            },
            }));
    }

    void SceneDebugEditor::ClearHistory()
    {
        undoRedoHistory_.Clear();
        savedRevision_ = Editor::EditorCommandStack::Get().GetSceneRevision();
    }

    void SceneDebugEditor::Update()
    {
        // プレハブモード：編集が止まるたびに書き戻す。置いた 1 体が消えていたら閉じる
        if (prefabMode_.IsOpen() && gameObjectManager_ && !prefabMode_.Update(*gameObjectManager_)) {
            ClosePrefabMode();
        }

        // デバッグ / リリースカメラの切り替え
        if (auto* inputManager = engine_->GetService<InputManager>()) {
            auto& input = inputManager->GetQuery();
            // ギズモの切り替えは割り当てを引くので、問い合わせ先を渡しておく
            objectSelector_.SetInputQuery(&input);
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
                undoRedoHistory_.Undo();
            }
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal)) {
                undoRedoHistory_.Redo();
            }
        }

        // Ctrl+S でシーン全体保存（プレハブモードの間は直すたびに保存しているので知らせるだけ）
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
            SaveScene();
        }

        // Ctrl+C でも選択中のオブジェクトを複製する（Ctrl+D と同じ）
        if (!ImGui::GetIO().WantTextInput &&
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, ImGuiInputFlags_RouteGlobal)) {
            DuplicateSelectedObject();
        }

        // 外でシーンのファイルが変わったか（git の pull など）
        CheckExternalChanges();

        // 保存通知オーバーレイの描画
        DrawSaveNotification();
    }

    void SceneDebugEditor::CheckExternalChanges()
    {
        const auto now = std::chrono::steady_clock::now();
        if (now < nextExternalCheck_) {
            return;
        }
        nextExternalCheck_ = now + std::chrono::seconds(1);

        const std::string sceneName = GetSceneName();
        SceneManager* const sceneManager = engine_ ? engine_->GetSceneManager() : nullptr;
        if (sceneName.empty() || !sceneManager || !sceneManager->CanLoadSceneNow()
            || !PlaybackStateManager::GetInstance().IsEditing() || prefabMode_.IsOpen()) {
            return;
        }

        std::vector<SceneSaveSystem::ExternalChange> changes = SceneSaveSystem::FindExternalChanges(sceneName);
        const auto sameChanges = [](const std::vector<SceneSaveSystem::ExternalChange>& a,
                                    const std::vector<SceneSaveSystem::ExternalChange>& b) {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                [](const SceneSaveSystem::ExternalChange& x, const SceneSaveSystem::ExternalChange& y) {
                    return x.fileName == y.fileName && x.kind == y.kind;
                });
        };
        if (changes.empty()) {
            externalChanges_.clear();
            acknowledgedChanges_.clear();
            return;
        }
        if (sameChanges(changes, acknowledgedChanges_) || sameChanges(changes, externalChanges_)) {
            return;
        }

        // 保存していない変更が無ければ、その場で読み直す（ドラッグなどの操作の途中は待つ）
        if (!IsSceneDirty()) {
            if (ImGui::IsAnyItemActive()) {
                return;
            }
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                "SceneDebugEditor: シーン {} のファイルが外で {} 件変わったので読み直します", sceneName, changes.size());
            ShowStatus("外でシーンのファイルが変わったので読み直しました", Editor::Theme::kOk);
            ReloadFromDisk();
            return;
        }

        Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
            "SceneDebugEditor: シーン {} のファイルが外で {} 件変わりました（保存していない変更があるので確かめます）",
            sceneName, changes.size());
        externalChanges_ = std::move(changes);
    }

    void SceneDebugEditor::ReloadFromDisk()
    {
        const std::string sceneName = GetSceneName();
        SceneManager* const sceneManager = engine_ ? engine_->GetSceneManager() : nullptr;
        if (sceneName.empty() || !sceneManager) {
            return;
        }

        // 読み直した後も同じ視点で見られるよう、エディタの視点を控えておく
        if (cameraManager_) {
            CameraSceneStateIO::Save(sceneName, *cameraManager_);
        }
        externalChanges_.clear();
        acknowledgedChanges_.clear();
        saveConflicts_.clear();
        sceneManager->ChangeScene(sceneName, SceneTransition::TransitionType::None, 0.0f);
    }

    void SceneDebugEditor::KeepEditingDespiteExternalChanges()
    {
        acknowledgedChanges_ = std::move(externalChanges_);
        externalChanges_.clear();
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

        // 画面の中を指しているときだけ受ける（他の窓の操作でカメラが飛ばないように）
        if (isViewportHovered) {
            if (auto* const inputManager = engine_->GetService<InputManager>()) {
                const InputAction focus = InputActionFromString("EditorFocusSelection");
                if (focus != InputAction::Invalid
                    && inputManager->GetQuery().IsActionTriggered(focus)) {
                    FocusOnSelection();
                }
            }
        }

        if (camera3D) {
            // スクリプトの道具がマウスを使っている間は、左クリックでオブジェクトを選ばない
            Editor::SceneViewTools& tools = Editor::SceneViewTools::Get();
            objectSelector_.Update(gameObjectManager_, camera3D, normalizedMousePos, isViewportHovered && !tools.WantsMouse());

            // スクリプトの道具（ウィンドウの OnSceneGUI・コンポーネントのギズモ）は、選んだ物のギズモより下に描く
            Editor::SceneViewContext context;
            context.position = viewportPos;
            context.size = viewportSize;
            context.hovered = isViewportHovered;
            context.camera = camera3D;
            context.objects = gameObjectManager_;
            context.selected = objectSelector_.GetSelectedObject();
            tools.Draw(context);
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
        if (!saveSystem_ || saveSystem_->GetSceneName().empty() || !gameObjectManager_) {
            return false;
        }
        if (RefuseSaveWhilePlaying()) {
            return false;
        }
        if (prefabMode_.IsOpen()) {
            ShowStatus("プレハブモードでは、直すたびにプレハブへ保存しています", Editor::Theme::kOk);
            return false;
        }

        // 外で変わったファイルを消してしまうなら、書く前に止めて選ばせる
        std::vector<std::string> conflicts = saveSystem_->CheckSaveConflicts(*gameObjectManager_);
        SceneManager* const sceneManager = engine_ ? engine_->GetSceneManager() : nullptr;
        if (const auto* const scene = sceneManager ? dynamic_cast<Scene*>(sceneManager->GetCurrentScene()) : nullptr) {
            for (std::string& file : scene->CheckSceneSettingsConflicts()) {
                conflicts.push_back(std::move(file));
            }
        }
        if (!conflicts.empty()) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "SceneDebugEditor: 外で変わったファイル {} 件とぶつかるので、保存を止めました", conflicts.size());
            ShowStatus("外で変わったファイルとぶつかるので、保存を止めました", Editor::Theme::kWarn);
            saveConflicts_ = std::move(conflicts);
            return false;
        }
        return WriteScene(false);
    }

    bool SceneDebugEditor::SaveSceneOverwriting()
    {
        if (!saveSystem_ || saveSystem_->GetSceneName().empty() || !gameObjectManager_) {
            return false;
        }
        if (RefuseSaveWhilePlaying()) {
            return false;
        }
        return WriteScene(true);
    }

    bool SceneDebugEditor::WriteScene(bool overwrite)
    {
        saveSystem_->SaveScene(gameObjectManager_, overwrite);

        // Feature・既定の床・衝突マトリクスもシーンの一部として書く
        SceneManager* const sceneManager = engine_ ? engine_->GetSceneManager() : nullptr;
        if (auto* const scene = sceneManager ? dynamic_cast<Scene*>(sceneManager->GetCurrentScene()) : nullptr) {
            scene->SaveSceneSettings(overwrite);
        }

        // エディタの視点を自分だけの状態として控える
        if (cameraManager_) {
            CameraSceneStateIO::Save(saveSystem_->GetSceneName(), *cameraManager_);
        }

        savedRevision_ = Editor::EditorCommandStack::Get().GetSceneRevision();
        dirtyWithoutEdits_ = false;
        saveConflicts_.clear();
        // 残した外の変更は、保存していない変更が無くなった次の見回りで読み直す
        acknowledgedChanges_.clear();
        return true;
    }

    void SceneDebugEditor::ShowStatus(const char* message, const ImVec4& color) const
    {
        if (DockingUI* const dockingUI = engine_ ? engine_->GetDebugSubsystem()->GetDockingUI() : nullptr) {
            dockingUI->ShowStatusMessage(message, color);
        }
    }

    bool SceneDebugEditor::IsSceneDirty() const
    {
        return dirtyWithoutEdits_ || Editor::EditorCommandStack::Get().GetSceneRevision() != savedRevision_;
    }

    bool SceneDebugEditor::RefuseInPrefabMode() const
    {
        if (!prefabMode_.IsOpen()) {
            return false;
        }
        ShowStatus("プレハブモードではシーンのオブジェクトを足したり消したりできません。シーンへ戻ってから操作してください",
            Editor::Theme::kWarn);
        return true;
    }

    bool SceneDebugEditor::OpenPrefabMode(const AssetInfo& prefab)
    {
        if (!gameObjectManager_) {
            return false;
        }
        if (!PlaybackStateManager::GetInstance().IsEditing()) {
            ShowStatus("再生中はプレハブを開けません。停止してから開いてください", Editor::Theme::kWarn);
            return false;
        }
        if (prefabMode_.IsOpen()) {
            ClosePrefabMode();
        }

        const bool wasDirty = IsSceneDirty();
        GameObject* const object = prefabMode_.Open(*gameObjectManager_, cameraManager_, prefab);
        if (!object) {
            ShowStatus("プレハブを開けませんでした", Editor::Theme::kWarn);
            return false;
        }
        sceneDirtyBeforePrefabMode_ = wasDirty;
        closePrefabModeRequested_ = false;
        objectSelector_.SelectObject(object);
        // 全体と周りが見えるよう、F で寄るときより離れて見る
        FocusOnSelection(8.0f);
        return true;
    }

    void SceneDebugEditor::ClosePrefabMode()
    {
        closePrefabModeRequested_ = false;
        if (!prefabMode_.IsOpen() || !gameObjectManager_) {
            return;
        }
        prefabMode_.Close(*gameObjectManager_, cameraManager_);

        // プレハブを直しただけではシーンのファイルは変わらないので、開く前に保存済みなら保存済みに戻す
        if (!sceneDirtyBeforePrefabMode_) {
            savedRevision_ = Editor::EditorCommandStack::Get().GetSceneRevision();
            dirtyWithoutEdits_ = false;
        }
    }

    void SceneDebugEditor::DrawPrefabModeHeader()
    {
        namespace Theme = Editor::Theme;
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetFrameHeight() + style.FramePadding.y * 2.0f;
        const ImVec2 max(min.x + ImGui::GetContentRegionAvail().x, min.y + height);
        ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(Theme::kAccent), 3.0f);

        ImGui::SetCursorScreenPos(ImVec2(min.x + style.FramePadding.y, min.y + style.FramePadding.y));
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::WithAlpha(Theme::kDeepest, 0.35f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::WithAlpha(Theme::kDeepest, 0.55f));
        if (ImGui::Button("◀ シーンへ戻る")) {
            closePrefabModeRequested_ = true;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("直した値は保存済みです。シーンの表示に戻ります");
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(("◈ " + prefabMode_.GetFileName()).c_str());

        ImGui::SetCursorScreenPos(ImVec2(min.x, max.y + style.ItemSpacing.y));
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
        // ビューポートで選び直したときだけ、その行まで送る
        //（一覧の行をクリックしたときは送らない。すでに見えているので跳ねるだけになる）
        if (objectSelector_.ConsumeViewportSelection()) {
            GameObject* const selected = objectSelector_.GetSelectedObject();
            scrollTargetId_ = selected ? selected->GetObjectId() : ObjectId{};
            scrollFramesLeft_ = kScrollFrames;
        }
        // 送り先は ID からこのフレームの分を引き直す（消えていれば送らない）
        scrollTarget_ = (gameObjectManager_ && scrollTargetId_.IsValid())
            ? gameObjectManager_->FindObject(scrollTargetId_) : nullptr;

        BuildHierarchyLinks();

        // プレハブモードの間は、青い帯とプレハブの 1 体だけを出す
        if (prefabMode_.IsOpen()) {
            DrawPrefabModeHeader();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Editor::Theme::WithAlpha(Editor::Theme::kAccent, 0.10f));
            if (auto child = UI::Scope::ChildScope("##HierarchyObjectList")) {
                for (GameObject* const root : hierarchyRoots_) {
                    DrawHierarchyRow(*root);
                }
            }
            ImGui::PopStyleColor();
            if (closePrefabModeRequested_) {
                ClosePrefabMode();
            }
            return;
        }

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
                for (GameObject* const root : hierarchyRoots_) {
                    DrawHierarchyRow(*root);
                }
                ImGui::TreePop();
            }
        }

        // 送り終えたら手放す。見つからなかったとき（消えた・親が無効など）も持ち越さない
        if (scrollFramesLeft_ > 0) {
            --scrollFramesLeft_;
        }
        if (scrollFramesLeft_ <= 0) {
            scrollTargetId_ = {};
            scrollTarget_ = nullptr;
        }
    }

    void SceneDebugEditor::BuildHierarchyLinks()
    {
        hierarchyChildren_.clear();
        hierarchyRoots_.clear();
        if (!gameObjectManager_) {
            return;
        }

        for (const auto& object : gameObjectManager_->GetAllObjects()) {
            if (!object || !gameObjectManager_->IsShownInIsolation(*object)) {
                continue;
            }
            const TransformComponent* const transform = object->GetComponent<TransformComponent>();
            const TransformComponent* const parent = transform ? transform->GetParent() : nullptr;
            GameObject* const parentObject = parent ? parent->GetOwner() : nullptr;
            // 親がいてもシーンから消えていれば根として出す（迷子にしない）
            if (parentObject && parentObject != object.get()) {
                hierarchyChildren_[parentObject].push_back(object.get());
            }
            else {
                hierarchyRoots_.push_back(object.get());
            }
        }
    }

    bool SceneDebugEditor::IsAncestorOf(const GameObject& object, const GameObject& descendant) const
    {
        const TransformComponent* transform = descendant.GetComponent<TransformComponent>();
        for (int depth = 0; transform && depth < 64; ++depth) {
            const TransformComponent* const parent = transform->GetParent();
            if (!parent) {
                return false;
            }
            if (parent->GetOwner() == &object) {
                return true;
            }
            transform = parent;
        }
        return false;
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

        const auto found = hierarchyChildren_.find(&object);
        const bool hasChildren = (found != hierarchyChildren_.end()) && !found->second.empty();

        ImGui::PushID(&object);

        // 送り先が閉じた親の中にいると行そのものが描かれない。先に道を開けておく
        if (hasChildren && scrollTarget_ && IsAncestorOf(object, *scrollTarget_)) {
            ImGui::SetNextItemOpen(true);
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
            | ImGuiTreeNodeFlags_OpenOnArrow
            | ImGuiTreeNodeFlags_FramePadding;
        if (isSelected) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        if (!hasChildren) {
            // 子が無くても矢印の幅は空ける（名前の頭が揃う）
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const float rowHeight = ImGui::GetTextLineHeight();
        const bool open = ImGui::TreeNodeEx("##row", flags, "%s", "");
        // 矢印を押したときは開閉だけ。選び直さない
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            objectSelector_.SelectObject(&object);
        }
        const float rowRight = ImGui::GetItemRectMax().x;

        if (scrollTarget_ == &object && scrollFramesLeft_ > 0) {
            ImGui::SetScrollHereY(0.5f);
        }

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
        // 矢印の分だけ右へずらす（子の有無で名前の頭がずれないように）
        const ImVec2 glyphPos(rowMin.x + ImGui::GetTreeNodeToLabelSpacing(), rowMin.y);
        drawList->AddText(glyphPos, ImGui::GetColorU32(glyphColor), glyph);
        const float left = glyphPos.x + ImGui::CalcTextSize(glyph).x + 6.0f;

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

        // 子を続けて描く（開いているときだけ）
        if (hasChildren && open) {
            for (GameObject* const child : found->second) {
                if (child) {
                    DrawHierarchyRow(*child);
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneDebugEditor::DrawInspectorContent()
    {
        GameObject* const selectedSprite = objectSelector_.GetSelectedSprite();
        GameObject* const selected = selectedSprite ? selectedSprite : objectSelector_.GetSelectedObject();
        if (!selected) {
            UI::Hint("オブジェクトを選択してください");
            return;
        }

        Editor::ObjectInspector::Callbacks callbacks;
        if (!prefabMode_.IsOpen()) {
            callbacks.saveObject = [this](GameObject& object) {
                if (!RefuseSaveWhilePlaying()) {
                    saveSystem_->SaveObject(&object);
                }
                };
        }
        // 値を変えた操作が Undo を通っていなくても、編集中なら未保存として数える
        if (Editor::ObjectInspector::Draw(*selected, callbacks)
            && !PlaybackStateManager::GetInstance().IsInPlayMode() && !prefabMode_.IsOpen()) {
            MarkSceneDirty();
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

    void SceneDebugEditor::Deselect(const GameObject& object)
    {
        if (objectSelector_.GetSelectedObject() == &object || objectSelector_.GetSelectedSprite() == &object) {
            objectSelector_.ClearSelection();
        }
    }

    ObjectEditing::Context SceneDebugEditor::MakeObjectEditingContext()
    {
        ObjectEditing::Context context;
        context.manager = gameObjectManager_;
        context.beforeDestroy = [this](const GameObject& object) { Deselect(object); };
        return context;
    }

    void SceneDebugEditor::CreateEmptyObject()
    {
        if (RefuseInPrefabMode()) {
            return;
        }
        if (!gameObjectManager_) {
            return;
        }
        if (GameObject* const created = ObjectEditing::CreateEmpty(MakeObjectEditingContext(), ComputeDropPosition(nullptr))) {
            objectSelector_.SelectObject(created);
        }
    }

    void SceneDebugEditor::CreateParticleObject(ObjectEditing::ParticleKind kind)
    {
        if (RefuseInPrefabMode()) {
            return;
        }
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
        if (RefuseInPrefabMode()) {
            return;
        }
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
        if (RefuseInPrefabMode()) {
            return false;
        }
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
        if (RefuseInPrefabMode()) {
            return false;
        }
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

    void SceneDebugEditor::FocusOnSelection(float distanceScale)
    {
        GameObject* const selected = objectSelector_.GetSelectedObject();
        if (!selected || !cameraManager_) {
            return;
        }
        auto* const orbit = cameraManager_->GetControllerAs<OrbitFlyController>(CameraNames::Scene);
        if (!orbit) {
            return;
        }

        // 大きさが分かるものはそれが収まる距離まで、分からないもの（空のオブジェクト・
        // ライト・カメラなど）は手頃な距離で寄せる
        constexpr float kDefaultRadius = 1.5f;
        constexpr float kMinDistance = 1.0f;

        const TransformComponent* const transform = selected->GetComponent<TransformComponent>();
        Vector3 center = transform ? transform->GetWorldPosition() : Vector3{ 0.0f, 0.0f, 0.0f };
        float radius = kDefaultRadius;

        if (const auto* const mesh = selected->GetComponent<MeshRendererComponent>()) {
            const BoundingBox box = mesh->GetWorldBoundingBox();
            if (box.IsValid()) {
                center = box.GetCenter();
                const Vector3 size = box.GetSize();
                radius = (std::max)({ size.x, size.y, size.z }) * 0.5f;
            }
        }

        orbit->SetTarget(center);
        orbit->SetDistance((std::max)(radius * distanceScale, kMinDistance));
    }

    void SceneDebugEditor::SpawnModelFromFile(const std::string& modelFileName, const Vector2* normalizedDropPos)
    {
        if (RefuseInPrefabMode()) {
            return;
        }
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
            src->SetTranslate(ComputeDropPosition(normalizedDropPos));
        }

        // スポーンしたオブジェクトを選択状態にする
        objectSelector_.SelectObject(raw);

        // スポーン操作を Undo 履歴に記録する
        ObjectSpawnRecord spawnRecord;
        spawnRecord.objectId = raw->GetObjectId();
        spawnRecord.objectName = raw->GetName();
        spawnRecord.serializeKey = raw->GetSerializeKey();
        spawnRecord.modelPath  = modelFileName;
        if (auto* src = raw->GetComponent<ITransformSource>()) {
            spawnRecord.translate = src->GetTranslate();
            spawnRecord.rotate    = src->GetRotate();
            spawnRecord.scale     = src->GetScale();
        }
        undoRedoHistory_.Push(spawnRecord);
    }

    void SceneDebugEditor::SpawnPrefabFromFile(const std::string& prefabFileName, const Vector2* normalizedDropPos)
    {
        if (RefuseInPrefabMode()) {
            return;
        }
        const AssetInfo* info = FindAssetInfo(prefabFileName);
        if (!info || info->type != AssetType::Prefab) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "プレハブが見つかりません: {}", prefabFileName);
            return;
        }

        const Reflection::AssetRefValue prefab{ info->guid, ToAssetPath(*info) };
        GameObject* placed = PrefabSystem::Instantiate(*gameObjectManager_, prefab, info->name,
            ObjectEditing::MakeNewObjectKey(info->name));
        if (!placed) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                "プレハブからオブジェクトを作れませんでした: {}", prefab.path);
            return;
        }

        if (auto* src = placed->GetComponent<ITransformSource>()) {
            src->SetTranslate(ComputeDropPosition(normalizedDropPos));
        }
        objectSelector_.SelectObject(placed);

        // 置いた操作を Undo 履歴に記録する（戻すと消し、やり直すと同じ ID と状態で置き直す）
        const ObjectId id = placed->GetObjectId();
        const std::string name = placed->GetName();
        const std::string key = placed->GetSerializeKey();
        const json state = placed->Serialize();
        Editor::EditorCommandStack::Get().Push(std::make_unique<Editor::FunctionCommand>(
            name + " の配置",
            [id] {
                GameObjectManager* const manager = Editor::SceneAccess::Objects();
                if (GameObject* const target = manager ? manager->FindObject(id) : nullptr) {
                    Editor::SceneAccess::Deselect(*target);
                    target->Destroy();
                    manager->InvalidateReferences();
                }
            },
            [prefab, name, key, state, id] {
                GameObjectManager* const manager = Editor::SceneAccess::Objects();
                if (GameObject* const target = manager ? PrefabSystem::Instantiate(*manager, prefab, name) : nullptr) {
                    target->SetSerializeKey(key);
                    manager->AssignObjectId(*target, id);
                    target->Deserialize(state);
                    manager->InvalidateReferences();
                }
            },
            true, true));

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

        if (prefabMode_.IsOpen()) {
            if (ImGui::MenuItem("シーンへ戻る")) {
                closePrefabModeRequested_ = true;
            }
            ImGui::EndPopup();
            return;
        }

        if (object.IsPrefabInstance()) {
            ImGui::TextDisabled("%s", object.GetPrefab().GetPath().c_str());
            ImGui::Separator();
            const AssetInfo* const prefabInfo = AssetDatabase::GetInstance().FindAssetByGUID(object.GetPrefab().GetGuid());
            if (ImGui::MenuItem("プレハブを開く", nullptr, false, prefabInfo != nullptr)) {
                OpenPrefabMode(*prefabInfo);
            }
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

#endif // CORE_EDITOR
