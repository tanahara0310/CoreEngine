#pragma once

// 前方宣言
class BossObject;

/// @brief ボスAIのデバッグUI（日本語対応）
class BossAIDebugUI {
public:
    /// @brief 初期化
    /// @param boss ボスオブジェクト
    void Initialize(BossObject* boss);

    /// @brief 更新・描画
    void Update();

    /// @brief 表示状態を設定
    /// @param show 表示するならtrue
    void SetVisible(bool show) { isVisible_ = show; }

    /// @brief 表示状態を取得
    /// @return 表示中ならtrue
    bool IsVisible() const { return isVisible_; }

private:
    /// @brief メインウィンドウを表示（すべての情報を1つのウィンドウに統合）
    void ShowMainWindow();

    /// @brief ノードツリーを再帰的に描画
    /// @param node ノード
    /// @param nodeName ノード名
    /// @param depth 深さ
    void DrawNodeTree(class BehaviorNode* node, const char* nodeName, int depth = 0);

    BossObject* boss_ = nullptr;
    bool isVisible_ = true;

    // ウィンドウ名
    static constexpr const char* mainWindowName_ = "ボスAIデバッグ";
};
