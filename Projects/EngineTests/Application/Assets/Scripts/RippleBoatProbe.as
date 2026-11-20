using namespace CoreEngine;

// 波紋の試験：置いた位置から前（回転 Y の向き）へ決まった距離を往復する。端で向きを変える
[DisplayName("試験：進む舟")]
class RippleBoatProbe : ScriptComponent
{
    [Tooltip("進む速さ [m/s]（0 で止める）")]
    float speed = 3.0f;

    [Tooltip("片道の距離 [m]")]
    float distance = 30.0f;

    private Vector3 start_;
    private float yaw_ = 0.0f;
    private float travelled_ = 0.0f;
    private bool returning_ = false;

    void Start()
    {
        start_ = owner.transform.position;
        yaw_ = owner.transform.rotation.y;
    }

    void Update()
    {
        if (speed <= 0.0f || distance <= 0.0f) {
            return;
        }

        travelled_ += speed * Time::DeltaTime();
        if (travelled_ >= distance) {
            travelled_ -= distance;
            returning_ = !returning_;
        }

        const float along = returning_ ? distance - travelled_ : travelled_;
        const Vector3 forward(sin(yaw_), 0.0f, cos(yaw_));
        owner.transform.position = start_ + forward * along;

        Vector3 rotation = owner.transform.rotation;
        rotation.y = returning_ ? yaw_ + 3.14159265f : yaw_;
        owner.transform.rotation = rotation;
    }
}
