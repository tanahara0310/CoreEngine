// プロジェクトの Scripts/Editor にあるこのファイルは、エディタが起動のたびに原本（Engine/Templates/Scripts/Editor/ComponentEditor.as）から書き直す。
// 中身を変えるときは原本を直す。
//
// コンポーネントのインスペクタを自分で描くクラスの基底クラス（Unity の CustomEditor に当たる）。
// 継いだクラスに [CustomEditor("型名")] を付けると、その型のコンポーネントのインスペクタの中身を OnInspectorGUI が描く。
// 型名はスクリプトのクラス名（PatrolRoute など）か、エンジンのコンポーネントの型名（Rigidbody など）。
// 既定の欄は EditorGUI::DrawDefaultInspector() で描ける。
// 値を変えるときは、変える前に Editor::RecordObject(target, "名前") で控えると Ctrl+Z で戻せる。
// 1 つのクラスのオブジェクトを、同じ型のコンポーネントすべてで使い回す（メンバ変数はコンポーネントごとに分かれない）。
abstract class ComponentEditor
{
    // エンジンが呼ぶ前に入れる
    private GameObject@ target_;
    private ScriptComponent@ component_;

    // インスペクタに出しているコンポーネントの持ち主
    GameObject@ target { get const { return target_; } }

    // インスペクタに出しているコンポーネント（スクリプトのコンポーネントのとき）。
    // cast<型名>(component) で中身に触れる。エンジンのコンポーネントなら null（target.GetComponent で取る）
    ScriptComponent@ component { get const { return component_; } }

    // インスペクタの中身を描く。書かなければ既定の欄だけ出る
    void OnInspectorGUI() { EditorGUI::DrawDefaultInspector(); }

    // このコンポーネントを持つオブジェクトを選んでいる間、シーンビューを描くフレームごと（Handles:: と SceneView:: が使える）
    void OnSceneGUI() {}
}
