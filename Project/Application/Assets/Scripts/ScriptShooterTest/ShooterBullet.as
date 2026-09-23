// 弾：まっすぐ進み、敵に当たるか時間が来たら消える
[DisplayName("シューター：弾")]
class ShooterBullet : ScriptComponent
{
    [Transient] [Tooltip("速度（撃つ側が入れる）")]
    Vector3 velocity;

    [Tooltip("消えるまでの秒数")]
    float lifetime = 2.0f;

    private float age_ = 0.0f;

    void Update()
    {
        const float dt = Time::DeltaTime();
        owner.transform.position = owner.transform.position + velocity * dt;
        age_ += dt;
        if (age_ >= lifetime) {
            owner.Destroy();
        }
    }

    void OnTriggerEnter(Collision@ other)
    {
        if (other.layer == CollisionLayer::Enemy) {
            owner.Destroy();
        }
    }
}
