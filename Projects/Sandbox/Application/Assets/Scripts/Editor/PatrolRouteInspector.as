// 「巡回の道筋」（PatrolRoute）のインスペクタを描くクラス。
// 道筋のオブジェクトを選ぶと、インスペクタに点の数とボタンを足し、シーンビューに点を動かすハンドルを出す。
// 点を変える前に Editor::RecordObject で控えるので、Ctrl+Z で戻せる。
[CustomEditor("PatrolRoute")]
class PatrolRouteInspector : ComponentEditor
{
    void OnInspectorGUI()
    {
        EditorGUI::DrawDefaultInspector();
        PatrolRoute@ route = cast<PatrolRoute>(component);
        if (route is null) {
            return;
        }

        EditorGUI::Label("点の数", "" + route.points.length());
        if (EditorGUI::Button("点を足す")) {
            Editor::RecordObject(target, "道筋の点を足す");
            Vector3 last = route.points.length() > 0 ? route.points[route.points.length() - 1] : Vector3(0.0f, 0.0f, 0.0f);
            route.points.insertLast(last + Vector3(2.0f, 0.0f, 0.0f));
        }
        EditorGUI::SameLine();
        EditorGUI::enabled = route.points.length() > 2;
        if (EditorGUI::Button("最後の点を消す")) {
            Editor::RecordObject(target, "道筋の点を消す");
            route.points.removeLast();
        }
        EditorGUI::enabled = true;
        EditorGUI::SameLine();
        if (EditorGUI::Button("逆回りにする")) {
            Editor::RecordObject(target, "道筋を逆回りにする");
            route.points.reverse();
        }
        EditorGUI::TextDisabled("シーンビューの矢印をドラッグすると点が動きます");
    }

    void OnSceneGUI()
    {
        PatrolRoute@ route = cast<PatrolRoute>(component);
        if (route is null) {
            return;
        }
        Vector3 base = route.BasePosition();
        for (uint i = 0; i < route.points.length(); ++i) {
            Vector3 current = base + route.points[i];
            Handles::Label(current, "点 " + i, Vector4(1.0f, 0.9f, 0.5f, 1.0f));
            Vector3 moved = Handles::PositionHandle(int(i), current);
            if (!(moved == current)) {
                Editor::RecordObject(target, "道筋の点を動かす");
                route.points[i] = moved - base;
            }
        }
    }
}
