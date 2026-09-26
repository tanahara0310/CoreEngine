// 倒したときに浮かぶ点数：画面の上へ流れながら薄くなって消える
[DisplayName("シューティング：点数の表示")]
class ShootingScorePopup : ScriptComponent
{
    [Tooltip("1 秒あたりに動く量")]
    Vector3 drift = Vector3(0.0f, 1.5f, 3.0f);

    [Tooltip("消えるまでの秒数")]
    float lifetime = 0.8f;

    private float age_ = 0.0f;
    private Vector4 color_;
    private bool started_ = false;

    void Update()
    {
        Text3DRenderer@ text = owner.text3DRenderer;
        if (!started_) {
            started_ = true;
            color_ = text.color;
        }
        const float dt = Time::DeltaTime();
        age_ += dt;
        owner.transform.position = owner.transform.position + drift * dt;

        Vector4 color = color_;
        color.w = color_.w * Saturate(1.0f - age_ / lifetime);
        text.color = color;

        if (age_ >= lifetime) {
            owner.Destroy();
        }
    }
}
