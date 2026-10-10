// 「カーブで上下」（CurveMover）のインスペクタを描くクラス。高さと周期はスライダー、カーブはカーブの欄で描く。
// 毎フレーム、変える前に Editor::RecordObject で控える（変わらなければ何も積まない。ドラッグの間は 1 回にまとまる）。
[CustomEditor("CurveMover")]
class CurveMoverInspector : ComponentEditor
{
    void OnInspectorGUI()
    {
        CurveMover@ mover = cast<CurveMover>(component);
        if (mover is null) {
            EditorGUI::DrawDefaultInspector();
            return;
        }
        Editor::RecordObject(target, "カーブで上下の変更");
        mover.height = EditorGUI::Slider("高さ", mover.height, 0.0f, 10.0f);
        mover.period = EditorGUI::Slider("周期（秒）", mover.period, 0.1f, 20.0f);
        EditorGUI::CurveField("動き", mover.curve);
        EditorGUI::TextDisabled("点をドラッグで動かし、ダブルクリックで足し、右クリックで消します");
    }
}
