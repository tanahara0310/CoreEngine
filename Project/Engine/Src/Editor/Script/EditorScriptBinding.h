#pragma once

#ifdef CORE_EDITOR

#include <string>

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

    /// @brief シーンビューのマウスとカメラ（`SceneView` 名前空間）と、重ねて描く関数（`Handles` 名前空間）をスクリプトへ登録する
    /// @return すべて登録できたら true
    bool RegisterEditorSceneView(asIScriptEngine* engine);

    /// @brief スクリプトの OnGUI を呼ぶ間、EditorGUI を使えるようにする
    /// @details 抜けるときに、スクリプトが閉じ忘れた Begin〜 の範囲・PushID・字下げを戻す。
    class GUIScope
    {
    public:
        /// @param windowKey 描くウィンドウの名前（ノードエディタの表示の位置などをウィンドウごとに持つ）
        explicit GUIScope(const std::string& windowKey);
        ~GUIScope();

        GUIScope(const GUIScope&) = delete;
        GUIScope& operator=(const GUIScope&) = delete;
    };

    /// @brief スクリプトのウィンドウの OnSceneGUI を呼ぶ間、SceneView と Handles を使えるようにする
    class SceneGUIScope
    {
    public:
        SceneGUIScope();
        ~SceneGUIScope();

        SceneGUIScope(const SceneGUIScope&) = delete;
        SceneGUIScope& operator=(const SceneGUIScope&) = delete;
    };

    /// @brief エディタのスクリプト（EditorWindow とメニューの関数）を 1 回呼ぶ間の後始末
    /// @details 抜けるときに、RecordObject で控えたオブジェクトの変更を Undo に積み、
    ///          スクリプトが閉じ忘れた Undo のまとまりを閉じる。
    class CallScope
    {
    public:
        CallScope();
        ~CallScope();

        CallScope(const CallScope&) = delete;
        CallScope& operator=(const CallScope&) = delete;
    };

    /// @brief エディタのスクリプトを呼んでいる最中か（CallScope の中か）
    bool IsInEditorCall();

    /// @brief EditorGUI::Image などで読み込んだテクスチャを手放す
    void ReleaseEditorGUITextures();

    /// @brief EditorGUI::BeginNodeEditor で作ったノードエディタの状態を手放す
    void ReleaseEditorGUINodeEditors();
}

#endif // CORE_EDITOR
