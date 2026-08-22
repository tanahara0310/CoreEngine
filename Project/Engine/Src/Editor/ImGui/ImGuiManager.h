#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <memory>
#include <wrl.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>

#include "DockingUI.h"
#ifdef USE_IMGUI
#include "CanvasViewport.h"
#include "ProjectView.h"
#endif

// 前方宣言
namespace CoreEngine {
    class GraphicsCore;
    class PostEffectManager;
    class GameDebugUI;
}

namespace CoreEngine
{
/// @brief ImGui管理クラス
class ImGuiManager {
public:
/// @brief 初期化
/// @param hwnd
/// @param device
/// @param swaoChainDesc
/// @param srvHeap
void Initialize(HWND hwnd, CoreEngine::GraphicsCore* dxCommon);

/// @brief ImGuiの開始処理
/// @param postEffectManager PostEffectManagerへのポインタ（オプション）
/// @param gameDebugUI GameDebugUIへのポインタ（オプション）
    void Begin(CoreEngine::PostEffectManager* postEffectManager = nullptr, CoreEngine::GameDebugUI* gameDebugUI = nullptr);

/// @brief ImGuiの終了処理
void End();

/// @brief ImGuiの描画
/// @param commandList
void Draw();

/// @brief 終了処理
void Finalize();

/// @brief Gameビューポートを描画（PostEffectPass完了後、ImGui::Render前に呼ぶこと）
/// @param dxCommon GraphicsCoreへのポインタ
/// @param postEffectManager PostEffectManagerへのポインタ
void DrawGameViewport(GraphicsCore* dxCommon, PostEffectManager* postEffectManager, GameDebugUI* gameDebugUI = nullptr);

/// @brief 副ビューポート（メインウィンドウ外へ出した ImGui ウィンドウ）を描画する
/// @details メインビューポートの Present 完了後に呼ぶこと。副ウィンドウは
///          imgui のプラットフォーム層が自前のスワップチェーンとコマンドキューで描画するため、
///          エンジンのコマンドリスト記録中に呼んではいけない。
void RenderPlatformWindows();

/// @brief ドッキングUIへのアクセッサ
/// @return ドッキングUIへのポインタ
DockingUI* GetDockingUI() const { return dockingUI_.get(); }

/// @brief エディタUI（メニューバー・ドックスペース・各パネル）を表示中か
/// @details false のとき Game ビューをウィンドウ全面に描く。絵だけ確認したいときの一時退避用。
bool IsEditorUiVisible() const { return editorUiVisible_; }

/// @brief エディタUIの表示/非表示を切り替える
void ToggleEditorUi() { editorUiVisible_ = !editorUiVisible_; }

#ifdef USE_IMGUI
/// @brief Canvasプレビュービューポートへのアクセッサ
/// @return CanvasViewportへのポインタ
CanvasViewport* GetCanvasViewport() const { return canvasViewport_.get(); }

/// @brief プロジェクトビューへのアクセッサ
/// @return プロジェクトビューへのポインタ
ProjectView* GetProjectView() const { return projectView_.get(); }
#endif

/// @brief ウィンドウハンドルの取得
/// @return HWND
HWND GetHwnd() const { return hwnd_; }

private:
    HWND hwnd_ = nullptr; // ウィンドウハンドル
    GraphicsCore* dxCommon_ = nullptr; // DirectX共通クラスへのポインタ

    // サブモジュール
    std::unique_ptr<DockingUI> dockingUI_ = std::make_unique<DockingUI>();
#ifdef USE_IMGUI
    std::unique_ptr<CanvasViewport> canvasViewport_ = std::make_unique<CanvasViewport>();
    std::unique_ptr<ProjectView> projectView_ = std::make_unique<ProjectView>();
#endif

    bool editorUiVisible_ = true; ///< false = Game のみ全面表示（F11 で切り替え）

private: // メンバ関数
    /// @brief Game ビューをメインビューポート全面へ描く（エディタUI非表示時）
    /// @param textureHandle 表示するテクスチャの SRV
    void DrawFullscreenGameViewport(D3D12_GPU_DESCRIPTOR_HANDLE textureHandle);


    /// @brief レイアウトや見た目を変更
    void ApplyCustomTheme();

    /// @brief フレームの開始
    void StartNewFrame();
};
}
