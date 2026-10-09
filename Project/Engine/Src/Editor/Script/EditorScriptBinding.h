#pragma once

#ifdef CORE_EDITOR

class asIScriptEngine;

namespace CoreEngine::Editor::ScriptBinding
{
    /// @brief エディタのウィンドウの部品（`EditorGUI` 名前空間）をスクリプトへ登録する
    /// @return すべて登録できたら true
    bool RegisterEditorGUI(asIScriptEngine* engine);

    /// @brief 編集中のシーンを操作する関数（`Editor` 名前空間）をスクリプトへ登録する
    /// @details オブジェクトの作成・削除は Undo に積む。
    /// @return すべて登録できたら true
    bool RegisterEditorScene(asIScriptEngine* engine);

    /// @brief スクリプトの OnGUI を呼ぶ間、EditorGUI を使えるようにする
    /// @details 抜けるときに、スクリプトが戻し忘れた enabled・indentLevel・PushID を戻す。
    class GUIScope
    {
    public:
        GUIScope();
        ~GUIScope();

        GUIScope(const GUIScope&) = delete;
        GUIScope& operator=(const GUIScope&) = delete;
    };

    /// @brief エディタのスクリプトを 1 回呼ぶ間の後始末
    /// @details 抜けるときに、スクリプトが閉じ忘れた Undo のまとまりを閉じる。
    class CallScope
    {
    public:
        CallScope() = default;
        ~CallScope();

        CallScope(const CallScope&) = delete;
        CallScope& operator=(const CallScope&) = delete;
    };
}

#endif // CORE_EDITOR
