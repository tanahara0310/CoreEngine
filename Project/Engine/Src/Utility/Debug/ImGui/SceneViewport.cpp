#include "SceneViewport.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect/PostEffectManager.h"
#include "WinApp/WinApp.h"
#include "Gizmo.h"
#include "ObjectCommon/GameObjectManager.h"
#include "Camera/ICamera.h"


namespace CoreEngine
{

void SceneViewport::Initialize()
{
    objectSelector_ = std::make_unique<ObjectSelector>();
    objectSelector_->Initialize();
}

void SceneViewport::DrawSceneViewport(DirectXCommon* dxCommon, PostEffectManager* postEffectManager)
{
    // Sceneウィンドウを描画開始（背景透過・スクロール無効）
    if (ImGui::Begin("Scene", nullptr,
            ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_NoScrollWithMouse
                | ImGuiWindowFlags_NoBackground)) {

        // ウィンドウ内サイズ取得と描画サイズ計算（アスペクト比維持）
        ImVec2 winSize = ImGui::GetWindowSize();
        const float aspect = static_cast<float>(WinApp::kClientWidth) / static_cast<float>(WinApp::kClientHeight);
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

        // ビューポートの矩形情報を記録（スクリーン座標系）
        viewportPos_ = ImVec2(contentPos.x + offsetX, contentPos.y + offsetY);
        viewportSize_ = ImVec2(drawW, drawH);
        
        // ビューポートがホバー状態かチェック
        isViewportHovered_ = ImGui::IsWindowHovered();

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

        // ギズモ準備（Imageの後に設定）
        Gizmo::Prepare(viewportPos_, viewportSize_);
        
        // ImGuizmoが正しいDrawListを使用するように設定
        ImGuizmo::SetDrawlist();

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
    ImGui::End();
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

}
