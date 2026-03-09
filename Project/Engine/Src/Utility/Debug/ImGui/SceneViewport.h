#pragma once
#include <imgui.h>
#include "ObjectSelector.h"
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

    class SceneViewport {
    public: // メンバ関数
        /// @brief 初期化
        void Initialize();

        /// @brief シーンビューポートの描画
        /// @param dxCommon DirectXCommonへのポインタ
        /// @param postEffectManager PostEffectManagerへのポインタ（オプション）
        void DrawSceneViewport(DirectXCommon* dxCommon, PostEffectManager* postEffectManager = nullptr);

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

    private: // メンバ関数
        /// @brief ギズモ操作タイプ切替ツールバーを描画（ビューポート内オーバーレイ）
        void DrawGizmoToolbar();

        /// @brief ギズモアイコンテクスチャを読み込む
        void LoadGizmoIcons();

        /// @brief ツールバー（再生/停止/ポーズ）を描画
        void DrawPlaybackToolbar();

        /// @brief 再生制御アイコンテクスチャを読み込む
        void LoadPlaybackIcons();

        /// @brief 共通ビューポート描画
        void DrawViewportWindow(const char* windowName, DirectXCommon* dxCommon, PostEffectManager* postEffectManager, bool enableGizmo);

    private: // メンバ変数
        ImVec2 viewportPos_{};
        ImVec2 viewportSize_{};
        bool isViewportHovered_ = false;
        std::unique_ptr<ObjectSelector> objectSelector_;
        const ICamera* currentCamera_ = nullptr;    // 現在の3Dカメラ
        const ICamera* currentCamera2D_ = nullptr;  // 現在の2Dカメラ

        // ギズモアイコン用テクスチャハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE gizmoTranslateIcon_{};
        D3D12_GPU_DESCRIPTOR_HANDLE gizmoRotateIcon_{};
        D3D12_GPU_DESCRIPTOR_HANDLE gizmoScaleIcon_{};
        D3D12_GPU_DESCRIPTOR_HANDLE gizmoToggleIcon_{};
        bool iconsLoaded_ = false;

        // 再生制御アイコン用テクスチャハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE playIcon_{};
        D3D12_GPU_DESCRIPTOR_HANDLE pauseIcon_{};
        bool playbackIconsLoaded_ = false;

        // ツールバーの折り畳み状態
        bool isToolbarCollapsed_ = false;
    };
}
