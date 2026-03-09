#include "SceneViewport.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect/PostEffectManager.h"
#include "Graphics/TextureManager.h"
#include "WinApp/WinApp.h"
#include "Gizmo.h"
#include "ObjectCommon/GameObjectManager.h"
#include "Camera/ICamera.h"
#include "EngineSystem/PlaybackState.h"
#include "Utility/Logger/Logger.h"
#include <string>
#include <format>


namespace CoreEngine
{

    void SceneViewport::Initialize()
    {
        objectSelector_ = std::make_unique<ObjectSelector>();
        objectSelector_->Initialize();

        // ギズモアイコンを読み込む
        LoadGizmoIcons();

        // 再生制御アイコンを読み込む
        LoadPlaybackIcons();
    }

    void SceneViewport::DrawSceneViewport(DirectXCommon* dxCommon, PostEffectManager* postEffectManager)
    {
        DrawViewportWindow("Scene", dxCommon, postEffectManager, true);
    }

    void SceneViewport::DrawGameViewport(DirectXCommon* dxCommon, PostEffectManager* postEffectManager)
    {
        DrawViewportWindow("Game", dxCommon, postEffectManager, false);
    }

    void SceneViewport::DrawViewportWindow(const char* windowName, DirectXCommon* dxCommon, PostEffectManager* postEffectManager, bool enableGizmo)
    {
        if (ImGui::Begin(windowName, nullptr,
            ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoBackground)) {

            // ウィンドウ内サイズ取得と描画サイズ計算（アスペクト比維持）
            ImVec2 winSize = ImGui::GetWindowSize();
            const float aspect = static_cast<float>(WinApp::GetCurrentClientWidthStatic()) /
                static_cast<float>(WinApp::GetCurrentClientHeightStatic());
            float drawW = winSize.x;
            float drawH = drawW / aspect;
            if (drawH > winSize.y) {
                drawH = winSize.y;
                drawW = drawH * aspect;
            }

            // ビューポート左上位置（中央寄せオフセット適用）
            ImVec2 contentPos = ImGui::GetCursorScreenPos();
            float offsetX = (winSize.x - drawW) * 0.5f;
            float offsetY = (winSize.y - drawH) * 0.5f;

            // カーソルを描画開始位置に移動
            ImGui::SetCursorScreenPos(ImVec2(
                contentPos.x + offsetX,
                contentPos.y + offsetY));

            if (enableGizmo) {
                viewportPos_ = ImVec2(contentPos.x + offsetX, contentPos.y + offsetY);
                viewportSize_ = ImVec2(drawW, drawH);
                isViewportHovered_ = ImGui::IsWindowHovered();
            }

            // シーンのレンダリング結果を表示
            // PostEffectManagerから最終テクスチャを取得（責務をカプセル化）
            D3D12_GPU_DESCRIPTOR_HANDLE textureHandle;

            if (postEffectManager) {
                // PostEffectManagerが管理する最終表示テクスチャを取得
                textureHandle = postEffectManager->GetFinalDisplayTextureHandle();
            } else {
                // PostEffectManagerが無い場合のフォールバック（1枚目のオフスクリーン）
                textureHandle = dxCommon->GetOffScreenSrvHandle();
            }

            ImTextureID texID = (ImTextureID)textureHandle.ptr;
            ImGui::Image(texID, ImVec2(drawW, drawH));

            if (enableGizmo) {
                // ギズモ準備（Imageの後に設定）
                Gizmo::Prepare(viewportPos_, viewportSize_);

                // ImGuizmoが正しいDrawListを使用するように設定
                ImGuizmo::SetDrawlist();

                // ビューポート内にギズモ操作タイプ切替ツールバーを描画
                DrawGizmoToolbar();

                // ギズモの描画（Imageの後に描画することで、シーン上に重ねて表示）
                if (objectSelector_) {
                    // 3Dオブジェクトのギズモ描画
                    if (currentCamera_ && objectSelector_->GetSelectedObject()) {
                        objectSelector_->DrawGizmo(currentCamera_);
                    }
                    // 2Dスプライトのギズモ描画
                    else if (currentCamera2D_ && objectSelector_->GetSelectedSprite()) {
                        objectSelector_->DrawGizmo2D(currentCamera2D_);
                    }
                }
            }
        }
        ImGui::End();
    }

    void SceneViewport::DrawGizmoToolbar()
    {
        if (!objectSelector_ || !iconsLoaded_) return;

        struct ToolButton {
            const char* tooltip;
            Gizmo::Mode mode;
            ImGuiKey shortcutKey;
            D3D12_GPU_DESCRIPTOR_HANDLE iconHandle;
        };

        const ToolButton kButtons[] = {
            { "移動  [W]", Gizmo::Mode::Translate, ImGuiKey_W, gizmoTranslateIcon_ },
            { "回転  [E]", Gizmo::Mode::Rotate,    ImGuiKey_E, gizmoRotateIcon_ },
            { "拡縮  [R]", Gizmo::Mode::Scale,     ImGuiKey_R, gizmoScaleIcon_ },
        };
        static const int kButtonCount = static_cast<int>(sizeof(kButtons) / sizeof(kButtons[0]));

        // ビューポートホバー中かつギズモ非操作中のみキーボードショートカットを受け付ける
        if (isViewportHovered_ && !ImGuizmo::IsUsing()) {
            for (const auto& btn : kButtons) {
                if (ImGui::IsKeyPressed(btn.shortcutKey, false)) {
                    objectSelector_->SetGizmoMode(btn.mode);
                }
            }
        }

        // スタイル設定
        constexpr float kIconSize = 25.0f;   // アイコンサイズ（テクスチャサイズ）
        constexpr float kPadding = 4.0f;    // パディング
        constexpr float kSpacing = 3.0f;    // スペース
        constexpr float kRounding = 6.0f;    // 丸み
        constexpr float kMargin = 8.0f;    // ビューポート端からのマージン
        constexpr float kToggleHeight = 18.0f;   // 折り畳みボタンの高さ（横長）

        const float buttonSize = kIconSize + kPadding * 2.0f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 toolbarOrigin = ImVec2(viewportPos_.x + kMargin, viewportPos_.y + kMargin);

        // 折り畳み状態に応じた背景サイズ計算
        float bgWidth;
        float bgHeight;

        if (isToolbarCollapsed_) {
            // 折り畳み時：折り畳みボタンのみ
            bgWidth = buttonSize;
            bgHeight = kToggleHeight;
        } else {
            // 展開時：折り畳みボタン＋アイコンボタン
            const float totalIconsHeight = buttonSize * kButtonCount + kSpacing * (kButtonCount - 1);
            bgWidth = buttonSize;
            bgHeight = kToggleHeight + kSpacing + totalIconsHeight;
        }

        // 背景矩形描画（ボタン領域にぴったり合わせる）
        drawList->AddRectFilled(
            toolbarOrigin,
            ImVec2(toolbarOrigin.x + bgWidth, toolbarOrigin.y + bgHeight),
            IM_COL32(20, 20, 20, 185),
            kRounding);

        // スタイル設定
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kPadding, kPadding));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, kSpacing));

        // 折り畳みボタンを描画（横長・テクスチャ）
        ImGui::SetCursorScreenPos(toolbarOrigin);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.10f, 0.10f, 1.00f));

        // 折りたたみ時は上向き（UV縦反転）、展開時は下向き（通常UV）
        const ImVec2 uvMin = isToolbarCollapsed_ ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
        const ImVec2 uvMax = isToolbarCollapsed_ ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
        if (ImGui::ImageButton("##GizmoToggle", (ImTextureID)gizmoToggleIcon_.ptr,
            ImVec2(kIconSize, kToggleHeight - kPadding * 2.0f), uvMin, uvMax)) {
            isToolbarCollapsed_ = !isToolbarCollapsed_;
        }

        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip(isToolbarCollapsed_ ? "ツールバーを展開" : "ツールバーを折り畳む");
        }

        // 展開時のみアイコンボタンを描画
        if (!isToolbarCollapsed_) {
            const Gizmo::Mode currentMode = objectSelector_->GetGizmoMode();

            for (int i = 0; i < kButtonCount; i++) {
                const auto& btn = kButtons[i];
                const bool isActive = (currentMode == btn.mode);

                // ボタン位置を明示的に設定（自動カーソルフローに依存しない）
                ImGui::SetCursorScreenPos(ImVec2(
                    toolbarOrigin.x,
                    toolbarOrigin.y + kToggleHeight + kSpacing + i * (buttonSize + kSpacing)));

                // ボタンスタイル設定
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.69f, 1.00f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.49f, 0.88f, 1.00f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.18f, 0.85f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.32f, 0.32f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.12f, 0.12f, 1.00f));
                }

                // アイコンボタンとして描画
                ImGui::PushID(i);
                if (ImGui::ImageButton(("##GizmoIcon" + std::to_string(i)).c_str(), (ImTextureID)btn.iconHandle.ptr, ImVec2(kIconSize, kIconSize))) {
                    objectSelector_->SetGizmoMode(btn.mode);
                }
                ImGui::PopID();

                ImGui::PopStyleColor(3);

                // ツールチップ表示
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s", btn.tooltip);
                }
            }
        }

        ImGui::PopStyleVar(3);
    }

    void SceneViewport::UpdateObjectSelection(GameObjectManager* gameObjectManager, const ICamera* camera)
    {
        if (!objectSelector_ || !gameObjectManager || !camera) {
            return;
        }

        // マウス座標をビューポート座標系に変換（0.0〜1.0の範囲）
        ImVec2 mousePos = ImGui::GetMousePos();
        Vector2 normalizedMousePos = Vector2(
            (mousePos.x - viewportPos_.x) / viewportSize_.x,
            (mousePos.y - viewportPos_.y) / viewportSize_.y
        );

        // オブジェクトセレクターを更新
        objectSelector_->Update(gameObjectManager, camera, normalizedMousePos, isViewportHovered_);
    }

    void SceneViewport::UpdateSpriteSelection(GameObjectManager* gameObjectManager, const ICamera* camera)
    {
        if (!objectSelector_ || !gameObjectManager || !camera) {
            return;
        }

        // マウス座標をビューポート座標系に変換（0.0〜1.0の範囲）
        ImVec2 mousePos = ImGui::GetMousePos();
        Vector2 normalizedMousePos = Vector2(
            (mousePos.x - viewportPos_.x) / viewportSize_.x,
            (mousePos.y - viewportPos_.y) / viewportSize_.y
        );

        // 2D用のオブジェクトセレクターを更新
        objectSelector_->Update2D(gameObjectManager, camera, normalizedMousePos, isViewportHovered_);
    }

    void SceneViewport::LoadGizmoIcons()
    {
        auto& texManager = TextureManager::GetInstance();

        // テクスチャマネージャーが初期化されているか確認
        if (!texManager.IsInitialized()) {
            return;
        }

        try {
            // ギズモアイコンを読み込む（Icon/Gizumoフォルダから）
            auto translateTex = texManager.Load("gizumoTransform.png");
            auto rotateTex = texManager.Load("gizumoRotate.png");
            auto scaleTex = texManager.Load("gizumoScale.png");
            auto toggleTex = texManager.Load("bottomArrow.png");

            // GPUハンドルを保存
            gizmoTranslateIcon_ = translateTex.gpuHandle;
            gizmoRotateIcon_ = rotateTex.gpuHandle;
            gizmoScaleIcon_ = scaleTex.gpuHandle;
            gizmoToggleIcon_ = toggleTex.gpuHandle;

            iconsLoaded_ = true;
        }
        catch (...) {
            // テクスチャ読み込みに失敗した場合は、アイコンを使用しない
            iconsLoaded_ = false;
        }
    }

    void SceneViewport::LoadPlaybackIcons()
    {
        auto& texManager = TextureManager::GetInstance();

        // テクスチャマネージャーが初期化されているか確認
        if (!texManager.IsInitialized()) {
            return;
        }

        // 再生制御アイコンを読み込む
        auto playTex = texManager.Load("reproduction.png");
        auto pauseTex = texManager.Load("pause.png");

        // GPUハンドルを保存
        playIcon_ = playTex.gpuHandle;
        pauseIcon_ = pauseTex.gpuHandle;

        playbackIconsLoaded_ = true;
    }

    void SceneViewport::DrawPlaybackToolbar()
    {
        if (!playbackIconsLoaded_) return;

        auto& playbackManager = CoreEngine::PlaybackStateManager::GetInstance();

        // スタイル設定
        constexpr float kIconSize = 32.0f;   // アイコンサイズ
        constexpr float kPadding = 6.0f;     // パディング
        constexpr float kSpacing = 8.0f;     // ボタン間スペース
        constexpr float kRounding = 6.0f;    // 丸み
        constexpr float kMarginTop = 8.0f;   // 上部マージン

        const float buttonSize = kIconSize + kPadding * 2.0f;
        const int kButtonCount = 3; // 再生、一時停止、停止

        // ツールバー全体の幅を計算
        const float toolbarWidth = buttonSize * kButtonCount + kSpacing * (kButtonCount - 1);

        // ビューポート中央上部に配置
        ImVec2 toolbarOrigin = ImVec2(
            viewportPos_.x + (viewportSize_.x - toolbarWidth) * 0.5f,
            viewportPos_.y + kMarginTop
        );

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 背景矩形描画
        drawList->AddRectFilled(
            toolbarOrigin,
            ImVec2(toolbarOrigin.x + toolbarWidth, toolbarOrigin.y + buttonSize),
            IM_COL32(20, 20, 20, 185),
            kRounding);

        // スタイル設定
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kPadding, kPadding));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kSpacing, 0.0f));

        CoreEngine::PlaybackState currentState = playbackManager.GetState();

        // 再生ボタン
        {
            ImGui::SetCursorScreenPos(toolbarOrigin);
            bool isActive = (currentState == CoreEngine::PlaybackState::Playing);

            if (isActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.69f, 1.00f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.49f, 0.88f, 1.00f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.18f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.32f, 0.32f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.12f, 0.12f, 1.00f));
            }

            if (ImGui::ImageButton("##PlayButton", (ImTextureID)playIcon_.ptr, ImVec2(kIconSize, kIconSize))) {
                playbackManager.Play();
            }

            ImGui::PopStyleColor(3);

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("再生 (Play)");
            }
        }

        // 一時停止ボタン
        {
            ImGui::SetCursorScreenPos(ImVec2(toolbarOrigin.x + buttonSize + kSpacing, toolbarOrigin.y));
            bool isActive = (currentState == CoreEngine::PlaybackState::Paused);

            if (isActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.69f, 1.00f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.49f, 0.88f, 1.00f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.18f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.32f, 0.32f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.12f, 0.12f, 1.00f));
            }

            if (ImGui::ImageButton("##PauseButton", (ImTextureID)pauseIcon_.ptr, ImVec2(kIconSize, kIconSize))) {
                playbackManager.Pause();
            }

            ImGui::PopStyleColor(3);

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("一時停止 (Pause)");
            }
        }

        ImGui::PopStyleVar(3);
    }

}
