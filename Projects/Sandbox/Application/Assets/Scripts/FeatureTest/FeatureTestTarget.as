// エディタ拡張のテスト（Tools > テスト > エディタ拡張のテスト の「シーン」の「インスペクタ」）で使うコンポーネント。
// ギズモの使い方の見本。インスペクタの中身は Editor/FeatureTest/FeatureTestTargetInspector.as が描く。
//
//   void OnDrawGizmos()          選んでいなくても、編集中のシーンビューに毎フレーム描く
//   void OnDrawGizmosSelected()  持ち主を選んでいる間だけ描く
//   Gizmos::color                次に描く線の色（呼ばれるたびに白から始まる）
//   Gizmos::DrawLine(始点, 終点)・DrawRay(始点, 向きと長さ)・DrawWireCube(中心, 大きさ)・DrawWireSphere(中心, 半径)・DrawSphere(中心, 半径)
// ギズモの関数は書き出したゲームでも呼べるが、何も描かない。
[DisplayName("テストの的")]
class FeatureTestTarget : ScriptComponent
{
    [Range(0.1f, 5.0f)]
    float radius = 1.0f;

    [Color]
    Vector4 color = Vector4(1.0f, 0.8f, 0.2f, 1.0f);

    [Tooltip("持ち主からのずれ。インスペクタの拡張がシーンビューに動かすハンドルを出す")]
    array<Vector3> points = { Vector3(2.0f, 0.0f, 0.0f), Vector3(2.0f, 0.0f, 2.0f), Vector3(0.0f, 0.0f, 2.0f) };

    [Tooltip("再生中に大きさを脈打たせるカーブ（横が 1 秒の中の位置、縦が倍率）")]
    array<Vector2> pulse = { Vector2(0.0f, 1.0f), Vector2(0.5f, 1.3f), Vector2(1.0f, 1.0f) };

    bool showGizmos = true;

    private float time_ = 0.0f;

    void Update()
    {
        time_ += Time::DeltaTime();
        float scale = Curve::Evaluate(pulse, fraction(time_));
        owner.transform.scale = Vector3(scale, scale, scale);
    }

    void OnDrawGizmos()
    {
        if (!showGizmos) {
            return;
        }
        Vector3 origin = owner.transform.worldPosition;
        Gizmos::color = color;
        Gizmos::DrawWireSphere(origin, radius);
        Gizmos::DrawRay(origin, Vector3(0.0f, radius * 2.0f, 0.0f));
    }

    void OnDrawGizmosSelected()
    {
        if (!showGizmos) {
            return;
        }
        Vector3 origin = owner.transform.worldPosition;
        Gizmos::color = Vector4(0.4f, 0.9f, 1.0f, 1.0f);
        Vector3 previous = origin;
        for (uint i = 0; i < points.length(); ++i) {
            Vector3 point = origin + points[i];
            Gizmos::DrawLine(previous, point);
            Gizmos::DrawSphere(point, 0.12f);
            previous = point;
        }
        Gizmos::color = Vector4(1.0f, 1.0f, 1.0f, 0.6f);
        Gizmos::DrawWireCube(origin, Vector3(radius * 2.0f, radius * 2.0f, radius * 2.0f));
    }
}
