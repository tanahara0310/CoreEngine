// 弾：まっすぐ進み、何かに当たるか、決めた範囲の外へ出たら消える。
// 当たる相手はシーンの衝突の組み合わせ（_scene.json の collision）で決まる
[DisplayName("シューティング：弾")]
class ShootingBullet : ScriptComponent
{
    [Transient] [Tooltip("速度（撃つ側が入れる）")]
    Vector3 velocity;

    [Tooltip("消えるまでの秒数")]
    float lifetime = 3.0f;

    [Tooltip("左右の範囲（X の絶対値がこれを超えたら消す）")]
    float limitX = 30.0f;

    [Tooltip("手前の範囲（Z がこれより小さくなったら消す）")]
    float minZ = -14.0f;

    [Tooltip("奥の範囲（Z がこれより大きくなったら消す）")]
    float maxZ = 24.0f;

    private float age_ = 0.0f;
    private bool dead_ = false;

    void Update()
    {
        if (dead_) {
            return;
        }
        const float dt = Time::DeltaTime();
        const Vector3 position = owner.transform.position + velocity * dt;
        owner.transform.position = position;

        age_ += dt;
        if (age_ >= lifetime || abs(position.x) > limitX || position.z < minZ || position.z > maxZ) {
            Vanish();
        }
    }

    void OnTriggerEnter(Collision@ other)
    {
        Vanish();
    }

    private void Vanish()
    {
        if (dead_) {
            return;
        }
        dead_ = true;
        owner.Destroy();
    }
}
