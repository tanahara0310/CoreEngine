#include "BaseScene.h"
#include "EngineSystem/EngineSystem.h"
#include "Engine/Camera/CameraManager.h"
#include "Engine/Camera/Debug/DebugCamera.h"
#include "Engine/Camera/Release/Camera.h"
#include "Engine/Camera/Camera2D.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Light/LightManager.h"
#include "Engine/Graphics/Render/RenderManager.h"
#include "Engine/Graphics/Render/Line/LineRendererPipeline.h"
#include "Engine/Graphics/Line/LineManager.h"
#include "Engine/Graphics/Render/Line/GridRenderer.h"
#include "Engine/Particle/ParticleSystem.h"
#include "Scene/SceneManager.h"
#include "Scene/SceneSerializer.h"
#include "Engine/ObjectCommon/SpriteObject.h"
#include "Engine/Utility/JsonManager/JsonManager.h"
#include "WinApp/WinApp.h"
#include <numbers>

#ifdef _DEBUG
#include <imgui.h>
#include "Engine/Camera/Debug/CameraDebugUI.h"
#include "Engine/Utility/Debug/ImGui/SceneViewport.h"
#include "Engine/Utility/Debug/ImGui/ObjectSelector.h"
#endif


namespace CoreEngine
{
    void BaseScene::Initialize(EngineSystem* engine)
    {
        engine_ = engine;

        //カメラ
        SetupCamera();

        //ライト
        SetupLight();

#ifdef _DEBUG
        //グリッド（デバッグビルドのみ）
        SetupGrid();

        // ギズモ変更時コールバックを設定
        auto imGuiManager = engine_->GetImGuiManager();
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
                            // SpriteObject の場合は独自トランスフォームから取得
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
        gameObjectManager_.SetEditCommitCallback([this](
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
                // SpriteObject の場合は独自トランスフォームから取得
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

        // 個別オブジェクト保存コールバック
        gameObjectManager_.SetOnSaveRequestCallback([this](GameObject* obj) {
            SaveSingleObjectToJson(obj);
            });
#endif
    }

    void BaseScene::Update()
    {
        // KeyboardInput を直接取得
        auto keyboard = engine_->GetComponent<KeyboardInput>();
        if (!keyboard) {
            return; // キーボードは必須
        }

#ifdef _DEBUG
        // デバッグカメラへの切り替え
        if (keyboard->IsKeyTriggered(DIK_F1)) {
            cameraManager_->SetActiveCamera("Debug", CameraType::Camera3D);
        } else if (keyboard->IsKeyTriggered(DIK_F2)) {
            cameraManager_->SetActiveCamera("Release", CameraType::Camera3D);

        }

#endif // _DEBUG


        // カメラの更新
        if (cameraManager_) {
            cameraManager_->Update();
        }

        // ライトマネージャーの更新
        auto lightManager = engine_->GetComponent<LightManager>();
        if (lightManager) {
            lightManager->UpdateAll();

            // シャドウマップ用のライトView-Projection行列を更新
            UpdateLightViewProjection();
        }


#ifdef _DEBUG
        // カメラマネージャーのImGui
        if (cameraManager_) {
            cameraManager_->DrawImGui();
        }

        // LineManagerのImGui
        LineManager::GetInstance().DrawImGui();

        // Ctrl+Z / Ctrl+Y によるキーボードショートカット（ウィンドウ外でも反応）
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
            undoRedoHistory_.Undo(&gameObjectManager_);
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) {
            undoRedoHistory_.Redo(&gameObjectManager_);
        }

        // Ctrl+S でシーン全体保存
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
            if (!sceneName_.empty()) {
                SaveObjectsToJson();
            }
        }

        // ゲームオブジェクトのImGuiデバッグUI表示
        if (ImGui::Begin("オブジェクト制御")) {
            // === 保存ボタン ===
            ImGui::BeginDisabled(sceneName_.empty());
            if (ImGui::Button("シーン全体保存 (Ctrl+S)")) {
                SaveObjectsToJson();
            }
            ImGui::EndDisabled();
            ImGui::Separator();

            // Undo/Redo ボタンと履歴件数を表示
            ImGui::BeginDisabled(!undoRedoHistory_.CanUndo());
            if (ImGui::Button("Undo")) {
                undoRedoHistory_.Undo(&gameObjectManager_);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!undoRedoHistory_.CanRedo());
            if (ImGui::Button("Redo")) {
                undoRedoHistory_.Redo(&gameObjectManager_);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text("(%d / %d)",
                undoRedoHistory_.GetUndoCount(),
                undoRedoHistory_.GetUndoCount() + undoRedoHistory_.GetRedoCount());
            ImGui::Separator();

            gameObjectManager_.DrawAllImGui();
        }
        ImGui::End();

        // シーンビューポートでのオブジェクト選択とギズモ更新（デバッグビルドのみ）
        auto imGuiManager = engine_->GetImGuiManager();
        ICamera* activeCamera3D = cameraManager_->GetActiveCamera(CameraType::Camera3D);
        ICamera* activeCamera2D = cameraManager_->GetActiveCamera(CameraType::Camera2D);
        if (imGuiManager) {
            auto sceneViewport = imGuiManager->GetSceneViewport();
            if (sceneViewport) {
                // 3Dカメラを設定
                if (activeCamera3D) {
                    sceneViewport->SetCamera(activeCamera3D);
                    // 3Dオブジェクト選択を更新
                    sceneViewport->UpdateObjectSelection(&gameObjectManager_, activeCamera3D);
                }

                // 2Dカメラを設定
                if (activeCamera2D) {
                    sceneViewport->SetCamera2D(activeCamera2D);
                    // スプライト選択を更新
                    sceneViewport->UpdateSpriteSelection(&gameObjectManager_, activeCamera2D);
                }
            }
        }

        // 保存通知オーバーレイの描画
        DrawSaveNotification();
#endif

        // 派生クラスの更新処理（GameObjectの更新前）
        OnUpdate();

        // ゲームオブジェクトの更新
        gameObjectManager_.UpdateAll();

        // コリジョン判定（毎フレーム: 収集 → 判定）
        // ClearColliders()でコライダーリストのみリセット（previousCollisions_は保持してEnter/Stay/Exitを正しく検出）
        collisionManager_.ClearColliders();
        gameObjectManager_.RegisterAllColliders(&collisionManager_);
        collisionManager_.CheckAllCollisions();

        // 派生クラスの後処理（クリーンアップ前）
        OnLateUpdate();
    }

    void BaseScene::Draw()
    {
        auto renderManager = engine_->GetComponent<RenderManager>();
        auto dxCommon = engine_->GetComponent<DirectXCommon>();
        ICamera* activeCamera3D = cameraManager_->GetActiveCamera(CameraType::Camera3D);

        if (!renderManager || !dxCommon || !activeCamera3D) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = dxCommon->GetCommandList();

        // ===== RenderManagerによる統一描画システム =====
        // カメラマネージャーを設定（タイプ別カメラを自動選択）
        renderManager->SetCameraManager(cameraManager_.get());
        renderManager->SetCommandList(cmdList);

        // 全てのゲームオブジェクトを描画キューに追加
        gameObjectManager_.RegisterAllToRender(renderManager);

        // 一括描画（自動的にパスごとにソート・グループ化）
        // この中でGridRenderer、LineDrawable、ParticleSystemのデバッグラインが全て描画される
        renderManager->DrawAll();

        // フレーム終了時にキューをクリア
        renderManager->ClearQueue();

        // 描画完了後に削除マークされたオブジェクトをクリーンアップ
        gameObjectManager_.CleanupDestroyed();
    }

    void BaseScene::Finalize()
    {
        // ゲームオブジェクトをクリア（新システム）
        gameObjectManager_.Clear();

#ifdef _DEBUG
        // Undo/Redo 履歴をクリア
        undoRedoHistory_.Clear();
#endif
    }

    void BaseScene::SetupCamera()
    {
        auto dxCommon = engine_->GetComponent<DirectXCommon>();
        if (!dxCommon) {
            return;
        }

        // カメラマネージャーを作成
        cameraManager_ = std::make_unique<CameraManager>();

        // ===== 3Dカメラの設定 =====

        // リリースカメラを作成して登録（斜め上から俯瞰する視点）
        auto releaseCamera = std::make_unique<Camera>();
        releaseCamera->Initialize(dxCommon->GetDevice());
        releaseCamera->SetTranslate({ 0.0f, 24.0f, -24.0f });
        releaseCamera->SetRotate({ 0.8f, 0.0f, 0.0f });

        cameraManager_->RegisterCamera("Release", std::move(releaseCamera));

        // デバッグカメラを作成して登録
        auto debugCamera = std::make_unique<DebugCamera>();
        debugCamera->Initialize(engine_, dxCommon->GetDevice());
        cameraManager_->RegisterCamera("Debug", std::move(debugCamera));

        // デフォルトでリリースカメラをアクティブに設定
        cameraManager_->SetActiveCamera("Release", CameraType::Camera3D);

        // ===== 2Dカメラの設定 =====

        // 2Dカメラを作成して登録（スクリーンサイズは自動取得）
        auto camera2D = std::make_unique<Camera2D>();
        // 2Dカメラの初期位置：画面中央
        camera2D->SetPosition(Vector2{ 0.0f, 0.0f });
        camera2D->SetZoom(1.0f);

        cameraManager_->RegisterCamera("Camera2D", std::move(camera2D));

        // 2Dカメラをアクティブに設定
        cameraManager_->SetActiveCamera("Camera2D", CameraType::Camera2D);
    }

    void BaseScene::SetupLight()
    {
        // デフォルトのディレクショナルライトを設定
        auto lightManager = engine_->GetComponent<LightManager>();
        if (lightManager) {
            directionalLight_ = lightManager->AddDirectionalLight();
            if (directionalLight_) {
                directionalLight_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
                directionalLight_->direction = MathCore::Vector::Normalize({ 0.0f, -1.0f, 0.0f });
                directionalLight_->intensity = 1.0f;
                directionalLight_->enabled = true;
            }
        }
    }

#ifdef _DEBUG
    void BaseScene::SetupGrid()
    {
        // GridRendererを作成
        gridRenderer_ = CreateObject<GridRenderer>();
        gridRenderer_->Initialize();

        // デフォルト設定
        gridRenderer_->SetGridSize(100.0f);
        gridRenderer_->SetSpacing(1.0f);
        gridRenderer_->SetVisible(true);

        // グリッドはシーンデータに保存しない
        gridRenderer_->SetSerializeEnabled(false);
    }
#endif

    void BaseScene::UpdateLightViewProjection()
    {
        // ディレクショナルライトが有効な場合のみ、シャドウマップ用の行列を計算
        if (!directionalLight_ || !directionalLight_->enabled) {
            return;
        }

        // ライト位置を計算（ライト方向の逆方向、シーン中心から一定距離）
        Vector3 lightDir = MathCore::Vector::Normalize(directionalLight_->direction);
        Vector3 lightPos = MathCore::Vector::Multiply(-kShadowLightDistance, lightDir);

        // ライトのビュー行列（シーン中心を見る）
        Vector3 target = { 0.0f, 0.0f, 0.0f };
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        Matrix4x4 lightView = MathCore::Matrix::LookAt(lightPos, target, up);

        // シャドウマップ用の正射影行列
        Matrix4x4 lightProjection = MathCore::Rendering::Orthographic(
            -kShadowOrthoSize, kShadowOrthoSize,  // 左、上
            kShadowOrthoSize, -kShadowOrthoSize,  // 右、下
            kShadowNearPlane, kShadowFarPlane      // 近平面、遠平面
        );

        // View * Projection
        Matrix4x4 lightViewProjection = MathCore::Matrix::Multiply(lightView, lightProjection);

        // RenderManagerに設定
        auto renderManager = engine_->GetComponent<RenderManager>();
        if (renderManager) {
            renderManager->SetLightViewProjection(lightViewProjection);
        }
    }

    void BaseScene::RegisterSceneBGM(std::unique_ptr<SoundManager::SoundResource>* bgm) {
        sceneBGM_ = bgm;

        // 現在設定されているBGM音量を取得
        if (bgm && *bgm && (*bgm)->IsValid()) {
            baseBGMVolume_ = (*bgm)->GetVolume();
        }

        // SceneManager経由でBGMコールバックを登録
        if (sceneManager_) {
            sceneManager_->RegisterSceneBGMCallback([this](float volumeMultiplier) {
                if (sceneBGM_ && *sceneBGM_ && (*sceneBGM_)->IsValid()) {
                    // 基本音量 × トランジション倍率で音量を設定
                    (*sceneBGM_)->SetVolume(baseBGMVolume_ * volumeMultiplier);
                }
                });
        }
    }

    void BaseScene::LoadObjectsFromJson()
    {
        if (sceneName_.empty()) return;

        std::string filePath = "Assets/Scene/" + sceneName_ + ".json";
        auto& jsonManager = JsonManager::GetInstance();

        if (!jsonManager.FileExists(filePath)) return;

        json j = jsonManager.LoadJson(filePath);
        if (!j.contains("objects")) return;

        const json& objects = j["objects"];
        for (const auto& obj : gameObjectManager_.GetAllObjects()) {
            if (!obj || !obj->IsSerializeEnabled()) continue;
            const std::string& name = obj->GetName();
            if (name.empty()) continue;
            if (objects.contains(name)) {
                // SpriteObject の場合は専用シリアライザを使用
                auto* spriteObj = dynamic_cast<SpriteObject*>(obj.get());
                if (spriteObj) {
                    SceneSerializer::LoadSpriteObject(spriteObj, objects[name]);
                } else {
                    SceneSerializer::LoadObject(obj.get(), objects[name]);
                }
            }
        }
    }

    void BaseScene::SaveObjectsToJson()
    {
        if (sceneName_.empty()) return;

        std::string dirPath = "Assets/Scene";
        std::string filePath = dirPath + "/" + sceneName_ + ".json";
        auto& jsonManager = JsonManager::GetInstance();

        jsonManager.CreateJsonDirectory(dirPath);

        json j;
        for (const auto& obj : gameObjectManager_.GetAllObjects()) {
            if (!obj || !obj->IsSerializeEnabled()) continue;
            const std::string& name = obj->GetName();
            if (name.empty()) continue;
            // SpriteObject の場合は専用シリアライザを使用
            auto* spriteObj = dynamic_cast<SpriteObject*>(obj.get());
            if (spriteObj) {
                SceneSerializer::SaveSpriteObject(spriteObj, j["objects"][name]);
            } else {
                SceneSerializer::SaveObject(obj.get(), j["objects"][name]);
            }
        }

        jsonManager.SaveJson(filePath, j);

#ifdef _DEBUG
        ShowSaveNotification("シーン全体を保存しました: " + sceneName_ + ".json");
#endif
    }

    void BaseScene::SaveSingleObjectToJson(GameObject* obj)
    {
        if (sceneName_.empty() || !obj || !obj->IsSerializeEnabled()) return;
        const std::string& name = obj->GetName();
        if (name.empty()) return;

        std::string dirPath = "Assets/Scene";
        std::string filePath = dirPath + "/" + sceneName_ + ".json";
        auto& jsonManager = JsonManager::GetInstance();

        jsonManager.CreateJsonDirectory(dirPath);

        // 既存の JSON を読み込み（なければ空オブジェクト）
        json j;
        if (jsonManager.FileExists(filePath)) {
            j = jsonManager.LoadJson(filePath);
        }

        // 対象オブジェクトのエントリだけ更新
        auto* spriteObj = dynamic_cast<SpriteObject*>(obj);
        if (spriteObj) {
            SceneSerializer::SaveSpriteObject(spriteObj, j["objects"][name]);
        } else {
            SceneSerializer::SaveObject(obj, j["objects"][name]);
        }

        jsonManager.SaveJson(filePath, j);

#ifdef _DEBUG
        ShowSaveNotification("\"" + name + "\" を保存しました");
#endif
    }

#ifdef _DEBUG
    void BaseScene::ShowSaveNotification(const std::string& message)
    {
        saveNotificationMessage_ = message;
        saveNotificationEndTime_ = ImGui::GetTime() + kNotificationDuration;
    }

    void BaseScene::DrawSaveNotification()
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
#endif
}
