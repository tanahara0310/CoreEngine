#pragma once

#ifdef CORE_EDITOR

#include <imgui.h>

#include <string>

namespace CoreEngine::Script
{
    class BindingRegistrar;
}

namespace CoreEngine::Editor::ScriptBinding
{
    /// @brief Begin〜 と End〜 で組にする範囲の種類
    enum class GUIScopeKind
    {
        Id,
        Group,
        Child,
        TreeNode,
        TabBar,
        TabItem,
        Table,
        ListBox,
        Popup,
        Menu,
        NodeEditor,
        Node,
        NodeTitle,
        InputPin,
        OutputPin,
    };

    struct NodeEditorState;

    /// @brief OnGUI 1 回分の状態
    struct GUIFrameState
    {
        bool active = false;
        bool enabled = true;
        bool changed = false;
        int indentLevel = 0;
        int canvasCount = 0;
        bool hasCanvas = false;
        bool canvasHovered = false;
        ImVec2 canvasMin{};
        ImVec2 canvasMax{};

        /// 描いているウィンドウ（ノードエディタの状態をウィンドウごとに分ける）
        std::string windowKey;

        /// 開いているノードエディタと、この OnGUI で最後に閉じたノードエディタ
        NodeEditorState* openNodeEditor = nullptr;
        NodeEditorState* lastNodeEditor = nullptr;
    };

    /// @brief 今の OnGUI の状態
    GUIFrameState& FrameState();

    /// @brief 実行中のスクリプトに例外を投げる
    void ThrowScriptException(const std::string& message);

    /// @brief OnGUI の中でなければスクリプトの例外にする
    /// @param function 例外の文に入れる関数の名前
    bool RequireGUI(const char* function);

    /// @brief 部品 1 つを描く間の準備（OnGUI の中かを確かめ、enabled が false なら押せなくする）
    class WidgetScope
    {
    public:
        explicit WidgetScope(const char* function);
        ~WidgetScope();

        WidgetScope(const WidgetScope&) = delete;
        WidgetScope& operator=(const WidgetScope&) = delete;

        /// @brief 描いてよいか
        explicit operator bool() const { return ok_; }

    private:
        bool ok_ = false;
        bool disabled_ = false;
    };

    /// @brief 字下げを level 段にする（今のウィンドウの中で）
    void SetIndentLevel(int level);

    /// @brief ラベルのうち表示する部分（`##` より前）
    std::string LabelDisplayPart(const std::string& label);

    /// @brief 欄の ID（ラベルは左の列に描くので、欄そのものには表示しない）
    std::string LabelFieldId(const std::string& label);

    /// @brief 左の列にラベルを描き、次の欄を右の列へ置く（表示する部分が空なら欄を幅いっぱいにする）
    void BeginLabeledRow(const std::string& label);

    /// @brief sRGB の色を ImGui の描画先（リニア）の色にする
    ImVec4 SrgbToLinear(float r, float g, float b, float a);

    /// @brief スクリプトの Key（DirectInput のキーの番号）を ImGui のキーにする（無ければ ImGuiKey_None）
    ImGuiKey ToImGuiKey(int key);

    /// @brief 範囲を開いたことを控える
    /// @param open ImGui の Begin が true を返したか（Child と Group は常に true を渡す）
    void PushGUIScope(GUIScopeKind kind, bool open);

    /// @brief 一番内側の範囲を閉じる
    /// @details その上に残った PushID は先に戻す。一番内側が kind でなければスクリプトの例外にする。
    /// @return 閉じたら true
    bool PopGUIScope(GUIScopeKind kind, const char* function);

    /// @brief 開いている範囲 1 つ
    struct GUIScopeEntry
    {
        GUIScopeKind kind = GUIScopeKind::Id;

        /// ImGui の Begin が true を返したか
        bool open = false;

        /// 開いたときの字下げの段
        int savedIndent = 0;
    };

    /// @brief 一番内側の、ウィンドウを開く範囲（子の枠・一覧・ポップアップ・メニュー・ノードエディタ・ノード）か表
    /// @return 無ければ nullptr
    const GUIScopeEntry* InnermostWindowOrTable();

    /// @brief 一番内側の範囲（PushID は除く）
    /// @return 無ければ nullptr
    const GUIScopeEntry* InnermostScope();

    /// @brief ノードエディタかノードの中なら、置けない部品としてスクリプトの例外にする
    /// @return 置いてよければ true
    bool RejectInsideNodeEditor(const char* function);

    /// @brief ノードエディタの範囲を閉じる（EditorGUINodeBinding が実装する）
    void CloseNodeScope(GUIScopeKind kind);

    /// @brief 範囲と字下げをすべて戻す（OnGUI を抜けるときに呼ぶ）
    void UnwindGUIScopes();

    /// @brief Begin〜 と End〜 で組にする部品（子の枠・木・タブ・表・一覧・ポップアップ・メニュー）を登録する
    /// @note EditorGUI 名前空間を開いた BindingRegistrar を渡す。
    void RegisterEditorGUIContainers(Script::BindingRegistrar& r);

    /// @brief オブジェクトとアセットを選ぶ欄・画像の部品を登録する
    /// @note EditorGUI 名前空間を開いた BindingRegistrar を渡す。
    void RegisterEditorGUIAssets(Script::BindingRegistrar& r);

    /// @brief ノードエディタの部品（ノード・ピン・つながり）を登録する
    /// @note EditorGUI 名前空間を開いた BindingRegistrar を渡す。
    void RegisterEditorGUINodes(Script::BindingRegistrar& r);
}

#endif // CORE_EDITOR
