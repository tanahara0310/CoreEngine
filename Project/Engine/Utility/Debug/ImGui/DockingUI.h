#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>
#include <unordered_map>

/// @brief ドッキングエリアの定義

namespace CoreEngine
{
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

    /// @brief 登録されているウィンドウ一覧を取得（デバッグ用）
    const std::unordered_map<std::string, DockArea>& GetRegisteredWindows() const { return registeredWindows_; }

private:
    /// @brief エリアごとのノードIDを取得
    ImGuiID GetNodeIdForArea(DockArea area) const;

    /// @brief ドッキングレイアウトを構築
    void BuildDockLayout();

private:
    std::unordered_map<std::string, DockArea> registeredWindows_; // 登録されたウィンドウとそのエリア
    bool layoutInitialized_ = false; // レイアウトが初期化されたかどうか
    
    // エリアごとのノードID
    ImGuiID nodeIds_[7] = {0}; // DockAreaの数だけ（Bottomを追加したため7に変更）
};
}
