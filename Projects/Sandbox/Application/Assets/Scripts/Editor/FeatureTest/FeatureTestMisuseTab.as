using namespace CoreEngine;

// 「誤った使い方」のタブ。間違えた呼び方をしたときに、エディタが落ちずにスクリプトの例外になるかを試す
//
// ウィンドウの OnGUI で例外が起きると、そのウィンドウを止めて「もう一度動かす」を出す（Console にエラーの中身と場所が出る）。
// インスペクタの拡張で例外が起きると、そのクラスを止めて、いつもの欄と「もう一度動かす」を出す。
// どちらも、直して .as を保存すると読み直して動き始める。
// Begin〜 の閉じ忘れは、ほかの範囲の中なら外側の End〜 で順番違いの例外になる。一番外で閉じ忘れたものは、OnGUI を抜けるときに閉じる。

// テストの的のインスペクタ（FeatureTestTargetInspector）に、次に描くときに 1 回だけ例外を出させる
bool g_featureTestInspectorThrow = false;

class FeatureTestMisuseTab
{
    FeatureTestChecklist checks;
    string lastResult = "（まだ）";
    private bool forgetAtEnd_ = false;

    array<string> CheckItems()
    {
        array<string> items = {
            "例外になるボタンを押すと、このウィンドウが止まって「もう一度動かす」が出る",
            "Console に、どの関数をどう間違えたかと、スクリプトの場所が出る",
            "「もう一度動かす」で、このタブに戻って動き続ける",
            "「閉じ忘れ（タブの中）」は止まり、「閉じ忘れ（一番外）」は止まらずに動く",
            "「読めないプレハブ」は止まらず、null が返ったと出る",
            "「インスペクタで例外」の後に的を選ぶと、いつもの欄と「もう一度動かす」が出る",
            "どのボタンを押しても、エディタは落ちない"
        };
        return items;
    }

    // ウィンドウがタブを全部閉じた後に呼ぶ。頼まれていたら、グループを開いたまま OnGUI を抜ける
    void AfterTabs()
    {
        if (!forgetAtEnd_) {
            return;
        }
        forgetAtEnd_ = false;
        EditorGUI::BeginGroup();
        EditorGUI::Label("閉じていないグループ（OnGUI を抜けるときに閉じる）");
        lastResult = "一番外の BeginGroup を閉じずに OnGUI を抜けた";
    }

    void Draw()
    {
        checks.Draw(CheckItems());
        EditorGUI::HelpBox("下のボタンは、わざと間違えた呼び方をします。押すとこのウィンドウが止まるので、「もう一度動かす」で戻してください。",
            EditorGUI::MessageType::Warning);
        EditorGUI::Label("最後の結果", lastResult);

        EditorGUI::Header("範囲の組み方");
        if (EditorGUI::Button("End〜 だけを呼ぶ")) {
            EditorGUI::EndGroup();
        }
        if (EditorGUI::Button("Begin〜 と End〜 の順番違い")) {
            EditorGUI::BeginGroup();
            EditorGUI::BeginChild("misuse", 0, 40);
            EditorGUI::EndGroup();
        }
        if (EditorGUI::Button("閉じ忘れ（タブの中）")) {
            EditorGUI::BeginGroup();
            EditorGUI::Label("閉じていないグループ");
        }
        if (EditorGUI::Button("閉じ忘れ（一番外・止まらない）")) {
            forgetAtEnd_ = true;
        }

        EditorGUI::Header("呼べる場所の違い");
        if (EditorGUI::Button("時間軸の外で TimelineKeys")) {
            array<float> keys = { 1.0f };
            EditorGUI::TimelineKeys("外", keys);
        }
        if (EditorGUI::Button("インスペクタの外で DrawDefaultInspector")) {
            EditorGUI::DrawDefaultInspector();
        }
        if (EditorGUI::Button("ノードエディタの前に問い合わせ")) {
            EditorGUI::IsNodeEditorHovered();
        }
        if (EditorGUI::Button("OnGUI の中で Handles")) {
            Handles::DrawLine(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
        }
        if (EditorGUI::Button("OnGUI の中で SceneView")) {
            SceneView::UseMouse();
        }

        EditorGUI::Header("値の間違い");
        if (EditorGUI::Button("同じ番号のノード")) {
            EditorGUI::BeginNodeEditor("misuse", EditorGUI::NodeEditorFlags::None, 120);
            EditorGUI::BeginNode(1, Vector2(10.0f, 10.0f));
            EditorGUI::NodeTitle("1 つ目");
            EditorGUI::EndNode();
            EditorGUI::BeginNode(1, Vector2(200.0f, 10.0f));
            EditorGUI::NodeTitle("2 つ目");
            EditorGUI::EndNode();
            EditorGUI::EndNodeEditor();
        }
        if (EditorGUI::Button("使えないパスの DataFile")) {
            DataFile@ file = DataFile("../outside.json");
            lastResult = "DataFile を作れてしまった: " + file.path;
        }
        if (EditorGUI::Button("null のハンドル")) {
            GameObject@ nothing;
            lastResult = nothing.name;
        }
        if (EditorGUI::Button("throw で例外を出す")) {
            throw("エディタ拡張のテスト：throw で出した例外");
        }
        if (EditorGUI::Button("読めないプレハブ（止まらない）")) {
            GameObject@ created = Editor::InstantiatePrefab("Application/Assets/Prefabs/NoSuchPrefab.prefab", "FeatureTest 無い");
            lastResult = created is null ? "読めないプレハブは null が返った" : "作れてしまった";
        }

        EditorGUI::Header("インスペクタの拡張");
        if (EditorGUI::Button("インスペクタで例外")) {
            g_featureTestInspectorThrow = true;
            lastResult = "テストの的を選ぶと、インスペクタで例外が出る（インスペクタのタブの「的を作る」で作れる）";
        }
    }
}
