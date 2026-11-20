using namespace CoreEngine;

// 波紋の試験：決まった間隔でオーナーを置いた位置と向きへ戻して止め、水へ落とし直す
[DisplayName("試験：落とし直す")]
class RippleDropBoxProbe : ScriptComponent
{
    [Tooltip("落とし直す間隔 [s]（0 で落とし直さない）")]
    float interval = 6.0f;

    private Vector3 start_;
    private Vector3 startRotation_;
    private float timer_ = 0.0f;

    void Start()
    {
        start_ = owner.transform.position;
        startRotation_ = owner.transform.rotation;
    }

    void Update()
    {
        if (interval <= 0.0f) {
            return;
        }

        timer_ += Time::DeltaTime();
        if (timer_ < interval) {
            return;
        }
        timer_ -= interval;

        owner.transform.position = start_;
        owner.transform.rotation = startRotation_;
        Rigidbody@ body = owner.rigidbody;
        if (body !is null) {
            body.velocity = Vector3(0.0f, 0.0f, 0.0f);
            body.angularVelocity = Vector3(0.0f, 0.0f, 0.0f);
        }
    }
}
