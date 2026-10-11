// 「テストの的」（FeatureTestTarget）のインスペクタを描くクラス。インスペクタの拡張の使い方の見本
//
//   [CustomEditor("型名")]               その型のコンポーネントのインスペクタの中身を、このクラスの OnInspectorGUI が描く
//   target                              インスペクタに出しているコンポーネントの持ち主
//   component                           スクリプトのコンポーネント（cast<型名>(component) で中身に触れる）
//   EditorGUI::DrawDefaultInspector()   いつもの欄を描く（どれかの値を変えたフレームは true）
//   Editor::RecordObject(target, 名前)  変える前に控えると Ctrl+Z で戻せる（変わらなければ何も積まない。ドラッグの間は 1 つにまとまる）
//   void OnSceneGUI()                   持ち主を選んでいる間、シーンビューを描くフレームごと（Handles:: と SceneView:: が使える）
// このクラスのオブジェクトは 1 つで、同じ型のコンポーネントすべてで使い回す（メンバ変数はコンポーネントごとに分かれない）。
// OnInspectorGUI で例外が起きると、このクラスを止めて、いつもの欄と「もう一度動かす」を出す（直して保存しても動き始める）。
[CustomEditor("FeatureTestTarget")]
class FeatureTestTargetInspector : ComponentEditor
{
    bool showDefault = true;
    int defaultChanges = 0;

    void OnInspectorGUI()
    {
        // 「誤った使い方」のタブで頼まれたら、1 回だけ例外を出す
        if (g_featureTestInspectorThrow) {
            g_featureTestInspectorThrow = false;
            throw("エディタ拡張のテスト：テストの的のインスペクタで出した例外");
        }
        FeatureTestTarget@ subject = cast<FeatureTestTarget>(component);
        if (subject is null) {
            EditorGUI::DrawDefaultInspector();
            return;
        }
        EditorGUI::HelpBox("この欄は FeatureTestTargetInspector.as が描いています");
        showDefault = EditorGUI::Toggle("いつもの欄も出す", showDefault);
        if (showDefault) {
            if (EditorGUI::DrawDefaultInspector()) {
                ++defaultChanges;
            }
            EditorGUI::Label("いつもの欄で変えた回数", "" + defaultChanges);
            EditorGUI::Separator();
        }

        // ここから下で変える値を、変える前に控える
        Editor::RecordObject(target, "テストの的の変更");
        subject.radius = EditorGUI::Slider("半径（自前の欄）", subject.radius, 0.1f, 5.0f);
        subject.color = EditorGUI::ColorField("色（自前の欄）", subject.color);
        subject.showGizmos = EditorGUI::Toggle("ギズモを描く", subject.showGizmos);
        EditorGUI::CurveField("脈打つ大きさ", subject.pulse, Vector2(0.0f, 0.5f), Vector2(1.0f, 1.5f), 90);

        EditorGUI::Header("点");
        for (uint i = 0; i < subject.points.length(); ++i) {
            EditorGUI::PushID(int(i));
            subject.points[i] = EditorGUI::Vector3Field("点 " + (i + 1), subject.points[i]);
            EditorGUI::PopID();
        }
        if (EditorGUI::Button("点を足す")) {
            Vector3 last = subject.points.length() > 0 ? subject.points[subject.points.length() - 1] : Vector3(0.0f, 0.0f, 0.0f);
            subject.points.insertLast(last + Vector3(1.0f, 0.0f, 0.0f));
        }
        EditorGUI::SameLine();
        EditorGUI::enabled = subject.points.length() > 1;
        if (EditorGUI::Button("最後の点を消す")) {
            subject.points.removeLast();
        }
        EditorGUI::enabled = true;
        EditorGUI::Label("持ち主", target.name);
        EditorGUI::TextDisabled("シーンビューの矢印をドラッグすると点が動きます");
    }

    void OnSceneGUI()
    {
        FeatureTestTarget@ subject = cast<FeatureTestTarget>(component);
        if (subject is null) {
            return;
        }
        Vector3 origin = target.transform.worldPosition;
        Handles::DrawWireDisc(origin, Vector3(0.0f, 1.0f, 0.0f), subject.radius, subject.color);
        for (uint i = 0; i < subject.points.length(); ++i) {
            Vector3 current = origin + subject.points[i];
            Handles::Label(current + Vector3(0.0f, 0.3f, 0.0f), "点 " + (i + 1), Vector4(1.0f, 0.9f, 0.5f, 1.0f));
            Vector3 moved = Handles::PositionHandle(100 + int(i), current);
            if (!(moved == current)) {
                Editor::RecordObject(target, "的の点を動かす");
                subject.points[i] = moved - origin;
            }
        }
    }
}
