#pragma once
#include <imgui.h>
#include "ObjectSelector.h"
#include "SceneViewportGizmoController.h"
#include "SceneViewportWindowRenderer.h"
#include "SceneViewportSelectionController.h"
#include <memory>
#include <wrl.h>
#include <d3d12.h>

/// @brief シーンビューポートクラス

namespace CoreEngine
{

    class DirectXCommon;
    class PostEffectManager;
    class GameObjectManager;
    class ICamera;
    class Render;

    class SceneViewport {
    public: // メンバ関数
        /// @brief 初期化
        void Initialize();

        /// @brief シーンビューポートの描画
        /// @param dxCommon DirectXCommonへのポインタ
        /// @param render Renderへのポインタ
        /// @param postEffectManager PostEffectManagerへのポインタ（オプション）
        void DrawSceneViewport(DirectXCommon* dxCommon, Render* render, PostEffectManager* postEffectManager = nullptr);

        /// @brief ゲームビューポートの描画
        /// @param dxCommon DirectXCommonへのポインタ
        /// @param postEffectManager PostEffectManagerへのポインタ（オプション）
        void DrawGameViewport(DirectXCommon* dxCommon, PostEffectManager* postEffectManager = nullptr);

        /// @brief 3Dカメラを設定（ギズモ描画用）
        /// @param camera 3Dカメラ
        void SetCamera(const ICamera* camera) { currentCamera_ = camera; }

        /// @brief 2Dカメラを設定（スプライトギズモ描画用）
        /// @param camera 2Dカメラ
        void SetCamera2D(const ICamera* camera) { currentCamera2D_ = camera; }

        /// @brief Gameビュー用3Dカメラを設定（Sceneビュー上のカメラアイコン表示/操作用）
        /// @param camera Gameビューで使用する3Dカメラ
        void SetGameCamera3D(ICamera* camera) { currentGameCamera3D_ = camera; }

        /// @brief オブジェクト選択とギズモの更新（3D）
        /// @param gameObjectManager ゲームオブジェクトマネージャー
        /// @param camera カメラ
        void UpdateObjectSelection(GameObjectManager* gameObjectManager, const ICamera* camera);

        /// @brief スプライト選択とギズモの更新（2D）
        /// @param gameObjectManager ゲームオブジェクトマネージャー
        /// @param camera 2Dカメラ
        void UpdateSpriteSelection(GameObjectManager* gameObjectManager, const ICamera* camera);

        // ギズモ用にビューポートの矩形情報を取得
        ImVec2 GetViewportPos() const { return viewportPos_; }
        ImVec2 GetViewportSize() const { return viewportSize_; }

        /// @brief ビューポートがアクティブ（マウスがその上にあるか）
        /// @return ビューポートがアクティブならtrue
        bool IsViewportHovered() const { return isViewportHovered_; }

        /// @brief オブジェクトセレクターを取得
        /// @return オブジェクトセレクターへのポインタ
        ObjectSelector* GetObjectSelector() { return objectSelector_.get(); }

    private: // メンバ変数
        ImVec2 viewportPos_{};
        ImVec2 viewportSize_{};
        bool isViewportHovered_ = false;
        std::unique_ptr<ObjectSelector> objectSelector_;
        const ICamera* currentCamera_ = nullptr;    // 現在の3Dカメラ
        const ICamera* currentCamera2D_ = nullptr;  // 現在の2Dカメラ
        ICamera* currentGameCamera3D_ = nullptr; // Gameビュー用3Dカメラ

        // Scene/Gameウィンドウの描画責務を分離するレンダラー
        std::unique_ptr<SceneViewportWindowRenderer> windowRenderer_;

        // Sceneビューの選択更新責務を分離するコントローラー
        std::unique_ptr<SceneViewportSelectionController> selectionController_;

        // Sceneビューのギズモ関連描画責務を分離するコントローラー
        std::unique_ptr<SceneViewportGizmoController> gizmoController_;
    };
}
