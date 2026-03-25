#pragma once

#include <imgui.h>
#include <d3d12.h>

namespace CoreEngine
{
    class ICamera;
    class ObjectSelector;

    /// @brief SceneViewport上のギズモ関連描画に必要な入力データ
    struct SceneViewportDrawContext {
        ImVec2 viewportPos{};
        ImVec2 viewportSize{};
        bool isViewportHovered = false;
        const ICamera* currentCamera = nullptr;
        const ICamera* currentCamera2D = nullptr;
        ICamera* currentGameCamera3D = nullptr;
        ObjectSelector* objectSelector = nullptr;
    };

    /// @brief SceneViewportのギズモ/オーバーレイ描画責務を分離するコントローラー
    class SceneViewportGizmoController {
    public:
        /// @brief 初期化（Sceneビュー用アイコンの読み込み）
        void Initialize();

        /// @brief SceneViewport上のギズモ関連描画を実行
        /// @param context SceneViewportの描画コンテキスト
        void Draw(const SceneViewportDrawContext& context);

    private:
        /// @brief Sceneビューで使用するアイコンテクスチャを読み込む
        void LoadGizmoIcons();

        /// @brief Sceneビュー上にGameカメラアイコンを重ね描画
        void DrawGameCameraOverlay(const SceneViewportDrawContext& context);

        /// @brief Sceneビュー上でGameカメラのギズモ操作を描画
        void DrawGameCameraGizmo(const SceneViewportDrawContext& context);

    private:
        // Sceneビューオーバーレイ用テクスチャハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE gameCameraIcon_{};
        bool gameCameraIconLoaded_ = false;

        // UI状態
        bool isGameCameraSelected_ = false;
    };
}
