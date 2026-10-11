// 「インスペクタ」のタブ。インスペクタの拡張（ComponentEditor）とギズモを試すオブジェクトを作る
//
// 使っているファイル
//   Scripts/FeatureTest/FeatureTestTarget.as                 テストの的（コンポーネント）。OnDrawGizmos・OnDrawGizmosSelected と Gizmos::
//   Editor/FeatureTest/FeatureTestTargetInspector.as         テストの的のインスペクタ。[CustomEditor]・DrawDefaultInspector・OnSceneGUI
//   Editor/FeatureTest/FeatureTestRigidbodyInspector.as      剛体（エンジンのコンポーネント）のインスペクタに欄を足す
// スクリプトのコンポーネントは GameObject.GetComponent(@ハンドル) で取り出せる（エディタの道具からも、ゲームのスクリプトからも）。
class FeatureTestInspectorTab
{
    FeatureTestChecklist checks;

    array<string> CheckItems()
    {
        array<string> items = {
            "「的を作る」で、的のオブジェクトができて選ばれる",
            "インスペクタの「テストの的」に「この欄は FeatureTestTargetInspector.as が描いています」と出る",
            "「いつもの欄も出す」を外すと、いつもの欄が消えて自前の欄だけになる",
            "自前の半径・色・点・カーブを変えると、Ctrl+Z で戻る（変えていないフレームは Undo が増えない）",
            "いつもの欄で値を変えると「いつもの欄で変えた回数」が増える",
            "選んでいる間、シーンビューに点のハンドルと名前が出て、ドラッグで点が動き、Ctrl+Z で戻る",
            "選んでいなくても球の線と上向きの線（OnDrawGizmos）が出て、選ぶと点をつなぐ線と箱（OnDrawGizmosSelected）も出る",
            "「ギズモを描く」を外すと、ギズモが消える",
            "「剛体の的を作る」で、剛体のいつもの欄の下に欄が足され、「重さを 2 倍にする」が Ctrl+Z で戻る",
            "ほかの名前の剛体は、いつもの欄だけが出る",
            "再生すると、的がカーブに合わせて脈打つ"
        };
        return items;
    }

    void Draw()
    {
        checks.Draw(CheckItems());

        EditorGUI::Header("スクリプトのコンポーネントのインスペクタ");
        EditorGUI::Label("テストの的（FeatureTestTarget）を付けたオブジェクトを作って選びます。インスペクタの中身は FeatureTestTargetInspector が描き、シーンビューにはギズモとハンドルが出ます。");
        if (EditorGUI::Button("的を作る")) {
            CreateTarget();
        }

        GameObject@ selected = Editor::GetSelection();
        FeatureTestTarget@ subject;
        if (selected !is null && selected.GetComponent(@subject)) {
            EditorGUI::Label("選んでいる的の半径", formatFloat(subject.radius, "", 0, 2));
            EditorGUI::Label("選んでいる的の点の数", "" + subject.points.length());
        } else {
            EditorGUI::TextDisabled("的を選ぶと、ここにも値が出ます（GetComponent で取り出している）");
        }

        EditorGUI::Header("エンジンのコンポーネントのインスペクタ");
        EditorGUI::Label("剛体を付けたオブジェクトを作って選びます。FeatureTestRigidbodyInspector が、いつもの欄の下に欄を足します。");
        if (EditorGUI::Button("剛体の的を作る")) {
            Editor::BeginUndoGroup("剛体の的を作る");
            GameObject@ object = Editor::CreateObject("FeatureTest 剛体", Vector3(-3.0f, 1.0f, 0.0f));
            object.AddComponent("Rigidbody");
            Editor::EndUndoGroup();
            Editor::Select(object);
        }
    }

    private void CreateTarget()
    {
        Editor::BeginUndoGroup("的を作る");
        GameObject@ object = Editor::InstantiatePrefab("Application/Assets/Prefabs/MapGenerator/MapWall.prefab", "FeatureTest 的");
        if (object is null) {
            @object = Editor::CreateObject("FeatureTest 的");
        }
        object.transform.position = Vector3(0.0f, 0.5f, 0.0f);
        object.AddComponent("FeatureTestTarget");
        Editor::EndUndoGroup();
        Editor::Select(object);
    }
}
