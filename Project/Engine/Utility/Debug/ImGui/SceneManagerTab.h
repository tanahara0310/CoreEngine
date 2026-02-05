#pragma once

#include <string>
#include <vector>

namespace CoreEngine
{
// 前方宣言
class SceneManager;

/// @brief シーン管理用のImGuiタブクラス
class SceneManagerTab {
public:
    /// @brief 初期化
    /// @param sceneManager SceneManagerへのポインタ
    void Initialize(SceneManager* sceneManager);

    /// @brief ImGuiウィンドウを描画
    void DrawImGui();

    /// @brief シーン切り替えのリクエスト状態をリセット
    void ResetChangeRequest() { isChangeRequested_ = false; }

    /// @brief シーン切り替えがリクエストされているか確認
    /// @return リクエストされている場合true
    bool IsChangeRequested() const { return isChangeRequested_; }

    /// @brief リクエストされたシーン名を取得
    /// @return リクエストされたシーン名
    const std::string& GetRequestedSceneName() const { return requestedSceneName_; }

private:
    // ──────────────────────────────────────────────────────────
    // 各種UI描画メソッド
    // ──────────────────────────────────────────────────────────

    /// @brief 現在のシーン情報を描画
    void DrawCurrentSceneInfo();

    /// @brief シーン切り替えコントロールを描画
    void DrawSceneChangeControl();

    /// @brief シーン操作ボタンを描画
    void DrawSceneOperations();

    /// @brief シーン一覧を描画
    void DrawSceneList();

    /// @brief 現在のシーン名を更新
    void UpdateCurrentScene();

private:
    // ──────────────────────────────────────────────────────────
    // メンバ変数
    // ──────────────────────────────────────────────────────────

    SceneManager* sceneManager_ = nullptr; // SceneManagerへのポインタ

    // シーン切り替えリクエスト
    bool isChangeRequested_ = false;       // シーン切り替えリクエストフラグ
    std::string requestedSceneName_;       // リクエストされたシーン名
    int selectedSceneIndex_ = 0;           // ドロップダウンで選択されたインデックス

    // 前回のシーン名（シーン変更検出用）
    std::string previousSceneName_;

    // シーン検索・フィルタ
    char sceneSearchBuffer_[256] = "";     // シーン検索用バッファ
};
}
