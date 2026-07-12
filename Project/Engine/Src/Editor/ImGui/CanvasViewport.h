#pragma once

#ifdef USE_IMGUI

#include "Editor/ImGui/ImGuiAll.h"
#include "Math/Vector/Vector2.h"
#include <string>
#include <vector>

namespace CoreEngine
{
    class EngineSystem;
    class GameObjectManager;
    class TextureManager;
    class UIImage;

    /// @brief UI（UIImage）のみをプレビューする ImGui ウィンドウ
    /// @details
    ///  Engine の描画パイプラインには一切介入せず、ImGui の DrawList を使って
    ///  GameObjectManager から UIImage を抽出し、ウィンドウ内に直接描画する。
    ///  背景には専用テクスチャ（tile_black.png）を全面に敷き、
    ///  UI を見やすくするためのプレビュー領域として機能する。
    ///  Edit モードでは選択中 UIImage に対して移動・リサイズのギズモ操作が可能。
    class CanvasViewport
    {
    public:
        CanvasViewport()  = default;
        ~CanvasViewport() = default;

        /// @brief 初期化（背景テクスチャを読み込む）
        /// @param engine EngineSystem ポインタ（GameObjectManager 取得用）
        void Initialize(EngineSystem* engine);

        /// @brief Canvas プレビュー ウィンドウの描画
        void DrawCanvasViewport();

    private:
        /// @brief ギズモ操作モード
        enum class DragMode
        {
            None,
            Move,        // 本体ドラッグで移動
            ResizeTL,    // 左上ハンドル
            ResizeTR,    // 右上ハンドル
            ResizeBL,    // 左下ハンドル
            ResizeBR,    // 右下ハンドル
            Rotate,      // 回転ハンドル
        };

        /// @brief Edit モードでのギズモ操作（選択・移動・リサイズ）処理
        void HandleGizmoInteraction(
            const std::vector<UIImage*>& uiImages,
            const ImVec2& canvasMin,
            const ImVec2& canvasMax,
            const Vector2& referenceSize,
            float scale,
            const ImVec2& mousePos,
            bool mouseInCanvas,
            const Vector2& mouseRef,
            ImDrawList* drawList);

    private:
        EngineSystem* engine_ = nullptr;

        /// @brief 背景テクスチャの GPU ハンドル
        unsigned long long backgroundTextureHandlePtr_ = 0;
        Vector2 backgroundTextureSize_ = { 0.0f, 0.0f };

        // Edit モード（ギズモ操作）
        bool editMode_ = false;
        UIImage* selectedImage_ = nullptr;
        DragMode dragMode_ = DragMode::None;
        Vector2 dragStartMouseRef_ = { 0.0f, 0.0f };       // 基準解像度座標系のマウス開始位置
        Vector2 dragStartAnchoredPos_ = { 0.0f, 0.0f };
        Vector2 dragStartSize_ = { 0.0f, 0.0f };
        float dragStartRotation_ = 0.0f;                    // 回転開始時の角度（ラジアン）
        Vector2 dragRotatePivotScreen_ = { 0.0f, 0.0f };   // 回転中心（スクリーン座標）
    };

} // namespace CoreEngine

#endif // USE_IMGUI
