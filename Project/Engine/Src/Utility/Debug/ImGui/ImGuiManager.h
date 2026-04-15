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
#include "SceneViewport.h"
#include "ProjectView.h"
#endif

// 前方宣言
namespace CoreEngine {
    class DirectXCommon;
    class PostEffectManager;
    class GameDebugUI;
    class Render;
    class ICamera;
}

/// @brief ImGui管理クラス

namespace CoreEngine
{
class ImGuiManager {
public:
/// @brief 初期化
/// @param hwnd
/// @param device
/// @param swaoChainDesc
/// @param srvHeap
void Initialize(HWND hwnd, CoreEngine::DirectXCommon* dxCommon);

/// @brief ImGuiの開始処理
/// @param postEffectManager PostEffectManagerへのポインタ（オプション）
    /// @param render Renderへのポインタ（オプション）
/// @param gameDebugUI GameDebugUIへのポインタ（オプション）
    void Begin(CoreEngine::PostEffectManager* postEffectManager = nullptr, CoreEngine::Render* render = nullptr, CoreEngine::GameDebugUI* gameDebugUI = nullptr);

/// @brief ImGuiの終了処理
void End();

/// @brief ImGuiの描画
/// @param commandList
void Draw();

/// @brief 終了処理
void Finalize();

/// @brief ドッキングUIへのアクセッサ
/// @return ドッキングUIへのポインタ
DockingUI* GetDockingUI() const { return dockingUI_.get(); }

#ifdef USE_IMGUI
/// @brief シーンビューポートへのアクセッサ
/// @return シーンビューポートへのポインタ
SceneViewport* GetSceneViewport() const { return sceneViewport_.get(); }

/// @brief SceneViewウィンドウが前フレームで表示されていたか
/// @return 表示中ならtrue
bool IsSceneViewVisible() const { return sceneViewport_ && sceneViewport_->IsSceneViewVisible(); }

/// @brief プロジェクトビューへのアクセッサ
    /// @return プロジェクトビューへのポインタ
    ProjectView* GetProjectView() const { return projectView_.get(); }
#endif

/// @brief ウィンドウハンドルの取得
/// @return HWND
HWND GetHwnd() const { return hwnd_; }

private:
    HWND hwnd_ = nullptr; // ウィンドウハンドル
    DirectXCommon* dxCommon_ = nullptr; // DirectX共通クラスへのポインタ

    // サブモジュール
    std::unique_ptr<DockingUI> dockingUI_ = std::make_unique<DockingUI>();
#ifdef USE_IMGUI
    std::unique_ptr<SceneViewport> sceneViewport_ = std::make_unique<SceneViewport>();
    std::unique_ptr<ProjectView> projectView_ = std::make_unique<ProjectView>();
#endif

private: // メンバ関数
    /// @brief レイアウトや見た目を変更
    void ApplyCustomTheme();

    /// @brief フレームの開始
    void StartNewFrame();
};
}
