// カーブに沿って、持ち主を上下に動かすコンポーネント（親の無いオブジェクトに付ける）。
// カーブは、横が 1 周期の中の位置（0〜1）、縦が高さの割合（0〜1）。ゲームでは Curve::Evaluate で値を読む。
// インスペクタは CurveMoverInspector が、カーブを点のドラッグで直せる欄にして描く。
[DisplayName("カーブで上下")]
class CurveMover : ScriptComponent
{
    array<Vector2> curve = { Vector2(0.0f, 0.0f), Vector2(0.5f, 1.0f), Vector2(1.0f, 0.0f) };

    [Range(0.0f, 10.0f)]
    float height = 1.0f;

    [Range(0.1f, 20.0f)] [Tooltip("1 周期の秒数")]
    float period = 2.0f;

    private Vector3 origin_;
    private bool started_ = false;
    private float time_ = 0.0f;

    void Start()
    {
        origin_ = owner.transform.position;
        started_ = true;
    }

    void Update()
    {
        time_ += Time::DeltaTime();
        float phase = time_ / period - floor(time_ / period);
        owner.transform.position = origin_ + Vector3(0.0f, Curve::Evaluate(curve, phase) * height, 0.0f);
    }

    void OnDrawGizmosSelected()
    {
        Vector3 base = started_ ? origin_ : owner.transform.position;
        Gizmos::color = Vector4(0.4f, 1.0f, 0.6f, 1.0f);
        Gizmos::DrawLine(base, base + Vector3(0.0f, height, 0.0f));
        Gizmos::DrawSphere(base + Vector3(0.0f, height, 0.0f), 0.1f);
    }
}
