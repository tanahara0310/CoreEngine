#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <d3d12.h>
#include <string>
#include <vector>
#include <unordered_map>

/// @brief ドッキングエリアの定義

namespace CoreEngine
{
enum class DockLayoutPreset {
    Standard,
    Unity2By3
};

enum class DockArea {
    LeftTop,        // 左上（エンジン情報など）
    LeftBottom,     // 左下（カメラ情報など）
    Center,         // 中央（シーンビュー）
    Right,          // 右側（インスペクター）
    BottomLeft,     // 下部左（ライティング）
    BottomRight,    // 下部右（オブジェクト制御）
    Bottom          // 下部中央（プロジェクトビュー）
};

/// @brief ドッキングUI管理クラス（改良版）
class DockingUI {
public:
    /// @brief ドッキングエリアにウィンドウを登録
    /// @param windowName ウィンドウ名
    /// @param area ドッキングエリア
    void RegisterWindow(const std::string& windowName, DockArea area);

    /// @brief ウィンドウの登録を解除
    /// @param windowName ウィンドウ名
    void UnregisterWindow(const std::string& windowName);

    /// @brief ドッキングUIの初期化
    void BeginDockSpaceHostWindow();

    /// @brief ドッキングのセットアップ
    void SetupDockSpace();

    /// @brief レイアウトプリセットを設定
    void SetLayoutPreset(DockLayoutPreset preset);

    /// @brief 現在のレイアウトプリセットを取得
    DockLayoutPreset GetLayoutPreset() const { return layoutPreset_; }

    /// @brief 再生制御ツールバーを描画（メニューバーの直下）
    void DrawPlaybackToolbar();

    /// @brief 再生制御アイコンを読み込む
    void LoadPlaybackIcons();

    /// @brief 登録されているウィンドウ一覧を取得（デバッグ用）
    const std::unordered_map<std::string, DockArea>& GetRegisteredWindows() const { return registeredWindows_; }

    /// @brief ツールバーの高さを取得
    float GetToolbarHeight() const { return toolbarHeight_; }

    /// @brief グリッド表示状態を取得
    bool IsGridVisible() const { return isGridVisible_; }

    /// @brief グリッド表示状態を設定
    void SetGridVisible(bool visible) { isGridVisible_ = visible; }

private:
    /// @brief エリアごとのノードIDを取得
    ImGuiID GetNodeIdForArea(DockArea area) const;

    /// @brief ウィンドウ名とエリアから実際のドッキング先ノードIDを解決
    ImGuiID ResolveNodeIdForWindow(const std::string& windowName, DockArea area) const;

    /// @brief ドッキングレイアウトを構築
    void BuildDockLayout();

private:
    std::unordered_map<std::string, DockArea> registeredWindows_; // 登録されたウィンドウとそのエリア
    bool layoutInitialized_ = false; // レイアウトが初期化されたかどうか
    bool layoutDirty_ = false; // レイアウト再構築が必要かどうか
    DockLayoutPreset layoutPreset_ = DockLayoutPreset::Unity2By3;

    // エリアごとのノードID
    ImGuiID nodeIds_[7] = {0}; // DockAreaの数だけ（Bottomを追加したため7に変更）
    ImGuiID gameNodeId_ = 0;
    ImGuiID sceneNodeId_ = 0;
    ImGuiID toolNodeId_ = 0;

    // 再生制御アイコン用テクスチャハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE playIcon_{};
    D3D12_GPU_DESCRIPTOR_HANDLE pauseIcon_{};
    D3D12_GPU_DESCRIPTOR_HANDLE gridIcon_{};
    bool playbackIconsLoaded_ = false;

    // グリッド表示状態
    bool isGridVisible_ = true;

    // ツールバーの高さ
    static constexpr float toolbarHeight_ = 32.0f;
};
}
