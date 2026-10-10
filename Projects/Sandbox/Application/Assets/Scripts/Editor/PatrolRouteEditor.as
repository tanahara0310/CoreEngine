// 選んだオブジェクトの「巡回の道筋」（PatrolRoute）の点を、シーンビューのハンドルで動かすウィンドウ。Tools > 巡回の道筋 で開く。
// 点を動かす前に Editor::RecordObject で控えるので、Ctrl+Z で戻せる。
[MenuItem("Tools/巡回の道筋")]
class PatrolRouteEditor : EditorWindow
{
    void OnGUI()
    {
        GameObject@ selected = Editor::GetSelection();
        PatrolRoute@ route = FindRoute(selected);
        if (route is null) {
            EditorGUI::HelpBox("「巡回の道筋」を付けたオブジェクトを選ぶと、点をシーンビューのハンドルで動かせます。");
            if (EditorGUI::Button("道筋を持つオブジェクトを作る", -1)) {
                CreateRoute();
            }
            return;
        }

        EditorGUI::Header(selected.name + " の点");
        for (uint i = 0; i < route.points.length(); ++i) {
            EditorGUI::PushID(int(i));
            Vector3 value = EditorGUI::Vector3Field("点 " + i, route.points[i]);
            if (!(value == route.points[i])) {
                Editor::RecordObject(selected, "道筋の点を変える");
                route.points[i] = value;
            }
            EditorGUI::PopID();
        }
        if (EditorGUI::Button("点を足す")) {
            Editor::RecordObject(selected, "道筋の点を足す");
            Vector3 last = route.points.length() > 0 ? route.points[route.points.length() - 1] : Vector3(0.0f, 0.0f, 0.0f);
            route.points.insertLast(last + Vector3(2.0f, 0.0f, 0.0f));
        }
        EditorGUI::SameLine();
        EditorGUI::enabled = route.points.length() > 2;
        if (EditorGUI::Button("最後の点を消す")) {
            Editor::RecordObject(selected, "道筋の点を消す");
            route.points.removeLast();
        }
        EditorGUI::enabled = true;
        EditorGUI::HelpBox("シーンビューの矢印をドラッグすると点が動きます。Ctrl+Z で戻せます。");
    }

    void OnSceneGUI()
    {
        GameObject@ selected = Editor::GetSelection();
        PatrolRoute@ route = FindRoute(selected);
        if (route is null) {
            return;
        }
        Vector3 base = route.BasePosition();
        for (uint i = 0; i < route.points.length(); ++i) {
            Vector3 current = base + route.points[i];
            Handles::Label(current, "点 " + i, Vector4(1.0f, 0.9f, 0.5f, 1.0f));
            Vector3 moved = Handles::PositionHandle(int(i), current);
            if (!(moved == current)) {
                Editor::RecordObject(selected, "道筋の点を動かす");
                route.points[i] = moved - base;
            }
        }
    }

    private PatrolRoute@ FindRoute(GameObject@ object)
    {
        if (object is null) {
            return null;
        }
        PatrolRoute@ route;
        if (!object.GetComponent(@route)) {
            return null;
        }
        return route;
    }

    private void CreateRoute()
    {
        Editor::BeginUndoGroup("道筋を作る");
        GameObject@ object = Editor::CreateObject("PatrolRoute");
        object.AddComponent("PatrolRoute");
        Editor::EndUndoGroup();
        Editor::Select(object);
    }
}
