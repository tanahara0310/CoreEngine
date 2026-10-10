// 決めた点を順に回って、持ち主のオブジェクトを動かすコンポーネント（親の無いオブジェクトに付ける）。
// 点は、再生を始めたときの持ち主の位置からのずれで持つ。
// エディタでは、シーンビューに道筋を描き（OnDrawGizmos）、選んでいる間は点の周りに輪を描く（OnDrawGizmosSelected）。
// 点は Tools > 巡回の道筋 のハンドルでも動かせる。
[DisplayName("巡回の道筋")]
class PatrolRoute : ScriptComponent
{
    array<Vector3> points = { Vector3(0.0f, 0.0f, 0.0f), Vector3(4.0f, 0.0f, 0.0f), Vector3(4.0f, 0.0f, 4.0f), Vector3(0.0f, 0.0f, 4.0f) };

    [Range(0.0f, 20.0f)]
    float speed = 2.0f;

    [Tooltip("オンなら最後の点から最初の点へ戻る。オフなら来た道を折り返す")]
    bool loop = true;

    private Vector3 origin_;
    private bool started_ = false;
    private int target_ = 1;
    private int step_ = 1;

    void Start()
    {
        origin_ = owner.transform.position;
        started_ = true;
    }

    void Update()
    {
        if (points.length() < 2) {
            return;
        }
        Vector3 goal = origin_ + points[target_];
        Vector3 position = owner.transform.position;
        float distance = Distance(position, goal);
        float move = speed * Time::DeltaTime();
        if (distance <= move) {
            owner.transform.position = goal;
            NextTarget();
        } else {
            owner.transform.position = position + (goal - position) * (move / distance);
        }
    }

    void OnDrawGizmos()
    {
        Vector3 base = BasePosition();
        Gizmos::color = Vector4(0.3f, 0.8f, 1.0f, 1.0f);
        for (uint i = 0; i + 1 < points.length(); ++i) {
            Gizmos::DrawLine(base + points[i], base + points[i + 1]);
        }
        if (loop && points.length() > 2) {
            Gizmos::DrawLine(base + points[points.length() - 1], base + points[0]);
        }
        for (uint i = 0; i < points.length(); ++i) {
            Gizmos::DrawSphere(base + points[i], 0.12f);
        }
    }

    void OnDrawGizmosSelected()
    {
        Vector3 base = BasePosition();
        Gizmos::color = Vector4(1.0f, 0.75f, 0.2f, 1.0f);
        for (uint i = 0; i < points.length(); ++i) {
            Gizmos::DrawWireSphere(base + points[i], 0.35f);
        }
    }

    // 点の基準の位置（再生中は動き始めた位置、編集中は今の位置）
    Vector3 BasePosition()
    {
        return started_ ? origin_ : owner.transform.position;
    }

    private void NextTarget()
    {
        int count = int(points.length());
        if (loop) {
            target_ = (target_ + 1) % count;
            return;
        }
        if (target_ + step_ < 0 || target_ + step_ >= count) {
            step_ = -step_;
        }
        target_ += step_;
    }
}
