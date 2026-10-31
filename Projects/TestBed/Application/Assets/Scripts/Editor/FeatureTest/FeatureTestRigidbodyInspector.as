using namespace CoreEngine;

// エンジンのコンポーネント（剛体）のインスペクタを広げるクラス。インスペクタの拡張は、エンジンのコンポーネントにも付けられる。
// 型名にはエンジンのコンポーネントの型名（Rigidbody・Light など）を書く。component は null なので、中身は target から取る。
// テストのため、名前が FeatureTest で始まるオブジェクトだけ欄を足す（ほかの剛体は、いつもの欄だけを描く）。
[CustomEditor("Rigidbody")]
class FeatureTestRigidbodyInspector : ComponentEditor
{
    void OnInspectorGUI()
    {
        EditorGUI::DrawDefaultInspector();
        if (!IsTestObject()) {
            return;
        }
        Rigidbody@ body = target.rigidbody;
        if (body is null || !body.exists) {
            return;
        }
        EditorGUI::Header("FeatureTestRigidbodyInspector が足した欄");
        EditorGUI::Label("重さ", formatFloat(body.mass, "", 0, 2) + " kg");
        EditorGUI::Label("重力", body.useGravity ? "受ける" : "受けない");
        if (EditorGUI::Button("重さを 2 倍にする")) {
            Editor::RecordObject(target, "重さを 2 倍にする");
            body.mass = body.mass * 2.0f;
        }
        EditorGUI::SameLine();
        if (EditorGUI::Button("重力を切り替える")) {
            Editor::RecordObject(target, "重力を切り替える");
            body.useGravity = !body.useGravity;
        }
    }

    void OnSceneGUI()
    {
        if (!IsTestObject()) {
            return;
        }
        Vector3 origin = target.transform.worldPosition;
        Vector4 color = Vector4(1.0f, 0.5f, 0.3f, 1.0f);
        Handles::DrawLine(origin, origin + Vector3(0.0f, -1.5f, 0.0f), color, 3.0f);
        Handles::Label(origin + Vector3(0.0f, -1.8f, 0.0f), "重力の向き", color);
    }

    private bool IsTestObject()
    {
        return target !is null && target.name.findFirst("FeatureTest") == 0;
    }
}
