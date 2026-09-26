// 当たったときの火花：最初の更新でパーティクルを出し、しばらくしたら消える
[DisplayName("シューター：火花")]
class ShooterHitEffect : ScriptComponent
{
    [Tooltip("出す粒の数")]
    int count = 24;

    [Tooltip("消えるまでの秒数")]
    float lifetime = 1.2f;

    private float age_ = 0.0f;
    private bool emitted_ = false;

    void Update()
    {
        if (!emitted_) {
            emitted_ = true;
            ParticleSystem@ particles = owner.particleSystem;
            if (particles.exists) {
                particles.Emit(count);
            }
        }
        age_ += Time::DeltaTime();
        if (age_ >= lifetime) {
            owner.Destroy();
        }
    }
}
